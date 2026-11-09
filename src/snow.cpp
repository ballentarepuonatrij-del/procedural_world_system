#include "planet/snow.hpp"
#include <stdexcept>

namespace planet {
std::string_view snowExperimentName(SnowExperiment value) {
    constexpr std::array<std::string_view,6> names{"natural","fine","large","blizzard","drifting","thaw"};
    return names.at(static_cast<std::size_t>(value));
}
std::optional<SnowExperiment> snowExperiment(std::string_view text) {
    for(unsigned i=0;i<6;++i) if(text==snowExperimentName(static_cast<SnowExperiment>(i))) return static_cast<SnowExperiment>(i);
    return {};
}
double snowFraction(double temperature,double humidity) {
    if(!std::isfinite(temperature) || !std::isfinite(humidity)) return 0;
    // Wet-bulb depression approximation for the mixed rain/snow transition.
    const double wetBulb=temperature-3*(1-clamp(humidity,0.,1.));
    return 1-smoothstep(272.15,275.15,wetBulb);
}
double referenceSnowDepth(const Sample& s) {
    if(s.ocean || s.freshwater || !std::isfinite(s.temperature)) return 0;
    return 1.1*(1-smoothstep(257.15,275.15,s.temperature))*clamp(s.precipitation/850.,.1,1.4);
}
namespace {
double periodic(double x,double size) {return x-size*std::floor(x/size);}
void deposit(SnowCell& c,double amount,double density) {c.swe+=amount;c.depth+=amount*1000/density;}
void remove(SnowCell& c,double amount) {
    c.depth*=c.swe>0 ? std::max(0.,1-amount/c.swe) : 0;c.swe=std::max(0.,c.swe-amount);
}
}
SnowGrid::SnowGrid(int size,double spacing):size_(size),spacing_(spacing) {
    if(size<2 || size>128 || !std::isfinite(spacing) || spacing<=0) throw std::invalid_argument("Invalid snow grid");
    cells.resize(static_cast<std::size_t>(size*size));
}
double SnowGrid::waterVolume() const {double sum=0;for(const auto& c:cells) sum+=c.swe+c.airborne;return sum*spacing_*spacing_;}
SnowExchange SnowGrid::advance(double seconds,const SnowForcing& f) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>120 || !std::isfinite(f.temperature) || f.temperature<150 || f.temperature>350 ||
       !std::isfinite(f.humidity) || f.humidity<0 || f.humidity>1 || !std::isfinite(f.precipitation) || f.precipitation<0 || f.precipitation>.001 ||
       !std::isfinite(f.wind.east) || !std::isfinite(f.wind.north) || std::hypot(f.wind.east,f.wind.north)>150)
        throw std::invalid_argument("Invalid snow forcing");
    for(const auto& c:cells) if(!std::isfinite(c.bed+c.swe+c.depth+c.airborne+c.vegetation) || c.swe<0 || c.depth<0 || c.airborne<0 ||
        (c.swe>0 && c.depth<=0) || c.vegetation<0 || c.vegetation>1) throw std::invalid_argument("Invalid snow cell");
    SnowExchange result;if(seconds==0) return result;
    const double area=spacing_*spacing_,wind=std::hypot(f.wind.east,f.wind.north),phase=snowFraction(f.temperature,f.humidity);
    if(phase*f.precipitation==0 && waterVolume()==0) {for(auto& c:cells) c.meltRate=0;return result;}
    const double freshDensity=clamp(75+4*(f.temperature-258.15)+2*wind,65.,200.);
    std::vector<Wind2> velocities(cells.size());std::vector<double> delta(cells.size());
    const auto index=[&](int x,int z){return static_cast<std::size_t>(clamp(z,0,size_-1)*size_+clamp(x,0,size_-1));};
    for(int z=0;z<size_;++z) for(int x=0;x<size_;++x) {
        const auto i=index(x,z);auto& c=cells[i];c.meltRate=0;
        if(!c.solid) continue;
        const double snowfall=f.precipitation*phase*seconds;deposit(c,snowfall,freshDensity);result.snowfallM3+=snowfall*area;
        const double melt=std::min(c.swe,4e-3/86400*std::max(0.,f.temperature-273.15)*seconds);
        remove(c,melt);c.meltRate=melt/seconds;result.meltM3+=melt*area;
        const double sublimated=std::min(c.swe,2e-9*(1-f.humidity)*(1+wind)*seconds);
        remove(c,sublimated);result.sublimatedM3+=sublimated*area;
        if(c.swe>0) {
            const double density=mix(c.density(),400.,1-std::exp(-seconds*(1+wind*.05)/(5*86400)));
            c.depth=c.swe*1000/clamp(density,65.,450.);
        }
        const double dx=(cells[index(x+1,z)].bed-cells[index(x-1,z)].bed)/(2*spacing_);
        const double dz=(cells[index(x,z+1)].bed-cells[index(x,z-1)].bed)/(2*spacing_);
        // A falling terrain profile is in the lee: reduced near-ground flow
        // permits transported snow to settle. Vegetation adds roughness.
        const double lee=wind>0 ? clamp(-(dx*f.wind.east+dz*f.wind.north)/wind,0.,.8) : 0;
        const double exposure=(1-.72*lee/.8)*(1-.6*c.vegetation);
        velocities[i]={f.wind.east*.35*exposure,f.wind.north*.35*exposure};
    }
    const double dtLimit=std::min(1.,.65*spacing_/std::max(.01,.35*(std::abs(f.wind.east)+std::abs(f.wind.north))));
    for(double left=seconds;left>1e-10;) {
        const double dt=std::min(left,dtLimit);
        for(std::size_t i=0;i<cells.size();++i) {
            auto& c=cells[i];if(!c.solid) continue;
            const double exposure=wind>0 ? std::hypot(velocities[i].east,velocities[i].north)/(wind*.35) : 0;
            const double threshold=6.5+.015*std::max(0.,c.density()-100)+2*smoothstep(270.15,274.15,f.temperature);
            const double entrained=std::min(c.swe,1e-8*std::pow(std::max(0.,wind*exposure-threshold),3)*dt);
            remove(c,entrained);c.airborne+=entrained;
        }
        std::fill(delta.begin(),delta.end(),0.);
        const auto flux=[&](int a,int b,double speed) {
            const int donor=speed>=0 ? a : b,receiver=speed>=0 ? b : a;
            if(donor<0) return; // No untracked snow is imported from outside the local domain.
            const double amount=cells[static_cast<std::size_t>(donor)].airborne*std::abs(speed)*dt/spacing_;
            delta[static_cast<std::size_t>(donor)]-=amount;
            if(receiver>=0 && cells[static_cast<std::size_t>(receiver)].solid) delta[static_cast<std::size_t>(receiver)]+=amount;
            else result.exportM3+=amount*area; // Open water / cave masks and chart edges are explicit sinks.
            result.redistributedM3+=amount*area;
        };
        for(int z=0;z<size_;++z) for(int x=0;x<=size_;++x) {
            const int a=x>0 ? z*size_+x-1 : -1,b=x<size_ ? z*size_+x : -1;
            const double speed=(velocities[static_cast<std::size_t>(a<0 ? b : a)].east+velocities[static_cast<std::size_t>(b<0 ? a : b)].east)*.5;
            flux(a,b,speed);
        }
        for(int z=0;z<=size_;++z) for(int x=0;x<size_;++x) {
            const int a=z>0 ? (z-1)*size_+x : -1,b=z<size_ ? z*size_+x : -1;
            const double speed=(velocities[static_cast<std::size_t>(a<0 ? b : a)].north+velocities[static_cast<std::size_t>(b<0 ? a : b)].north)*.5;
            flux(a,b,speed);
        }
        for(std::size_t i=0;i<cells.size();++i) {
            auto& c=cells[i];c.airborne=std::max(0.,c.airborne+delta[i]);
            if(!c.solid) continue;
            const double settled=c.airborne*(1-std::exp(-dt*.45));
            c.airborne-=settled;deposit(c,settled,250);
        }
        left-=dt;
    }
    return result;
}
Snow::Snow(const World& w,Vec3 observer,double solar):config_(w.config()),anchor_(normalized(observer)),east_(east(anchor_)),north_(north(anchor_)) {
    setClock(solar);synchronize(w,observer);
}
void Snow::setClock(double seconds) {
    if(!std::isfinite(seconds) || std::abs(seconds)>1e12) throw std::invalid_argument("Invalid snow clock");
    solar_=seconds;
}
Vec3 Snow::directionAt(double x,double z) const {return normalized(anchor_+(east_*x+north_*z)/config_.radius);}
SnowCell Snow::makeCell(const World& w,int x,int z) const {
    const auto p=directionAt(x*spacing,z*spacing);const auto s=w.sample(p,solar_);
    SnowCell c;c.bed=s.elevation;c.solid=!s.ocean && !s.freshwater;
    if(const auto cave=w.caveSite(p);cave && cave->protectedGround(p)) c.solid=false;
    c.vegetation=s.biome==Biome::Forest || s.biome==Biome::Rainforest ? .75 : s.biome==Biome::Grassland ? .3 : .08;
    if(c.solid) {c.depth=referenceSnowDepth(s);c.swe=c.depth*.25;}
    return c;
}
void Snow::move(const World& w,Vec3 p,int x,int z,bool reset) {
    const double before=grid_.waterVolume();
    if(reset) {
        cache_.clear();cacheOrder_.clear();anchor_=normalized(p);east_=east(anchor_);north_=north(anchor_);driftX_=driftZ_=0;
    } else for(int j=0;j<resolution;++j) for(int i=0;i<resolution;++i) {
        const Key key{originX_+i,originZ_+j};
        if(!cache_.contains(key)) cacheOrder_.push_back(key);
        cache_[key]=grid_.cells[static_cast<std::size_t>(j*resolution+i)];
    }
    originX_=x;originZ_=z;displayDepth_.resize(grid_.cells.size());
    for(int j=0;j<resolution;++j) for(int i=0;i<resolution;++i) {
        const Key key{x+i,z+j};const auto found=cache_.find(key);const auto n=static_cast<std::size_t>(j*resolution+i);
        grid_.cells[n]=found==cache_.end() ? makeCell(w,key.first,key.second) : found->second;
        grid_.cells[n].meltRate=0;displayDepth_[n]=grid_.cells[n].depth;
    }
    while(cache_.size()>16384) {cache_.erase(cacheOrder_.front());cacheOrder_.pop_front();}
    if(initialized_) totals_.domainM3+=grid_.waterVolume()-before;else totals_.initialM3=grid_.waterVolume();
    initialized_=true;++revision_;
}
void Snow::synchronize(const World& w,Vec3 observer) {
    if(!(w.config()==config_)) throw std::invalid_argument("Reset snow after changing the planet");
    const bool next=w.modules().enabled(Module::Snow);if(enabled_!=next) ++revision_;enabled_=next;
    if(!active()) return;
    unsigned geometry=0;for(auto m:{Module::Terrain,Module::Ocean,Module::Landforms,Module::Caves,Module::Rivers,Module::InlandWaters})
        if(w.modules().enabled(m)) geometry|=1U<<static_cast<unsigned>(m);
    const double project=dot(observer,anchor_);
    if(!initialized_ || geometry!=geometry_ || project<.99 || length(observer-anchor_)*config_.radius>16000) {
        geometry_=geometry;move(w,observer,-resolution/2,-resolution/2,true);return;
    }
    const double x=dot(observer,east_)*config_.radius/project,z=dot(observer,north_)*config_.radius/project;
    if(std::abs(x-(originX_+resolution/2)*spacing)>extent/4 || std::abs(z-(originZ_+resolution/2)*spacing)>extent/4)
        move(w,observer,static_cast<int>(std::round(x/64))*8-resolution/2,static_cast<int>(std::round(z/64))*8-resolution/2,false);
}
SnowSample Snow::sampleLocal(double x,double z) const {
    if(!enabled_ || !initialized_ || !std::isfinite(x+z)) return {};
    const double gx=x/spacing-originX_,gz=z/spacing-originZ_;
    if(gx<0 || gz<0 || gx>=resolution-1 || gz>=resolution-1) return {};
    SnowSample s;s.available=true;s.weight=smoothstep(0.,3.,std::min({gx,gz,resolution-1-gx,resolution-1-gz}));
    const int ix=static_cast<int>(gx),iz=static_cast<int>(gz);const double tx=gx-ix,tz=gz-iz;
    for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
        const auto& c=grid_.cells[static_cast<std::size_t>((iz+dz)*resolution+ix+dx)];
        const double weight=(dx ? tx : 1-tx)*(dz ? tz : 1-tz);
        s.bed+=c.bed*weight;s.depth+=c.depth*weight;s.swe+=c.swe*weight;s.airborne+=c.airborne*weight;s.meltRate+=c.meltRate*weight;
    }
    return s;
}
SnowSample Snow::sample(Vec3 p) const {
    const double projection=dot(p,anchor_);if(projection<=0) return {};
    return sampleLocal(dot(p,east_)*config_.radius/projection,dot(p,north_)*config_.radius/projection);
}
double Snow::depth(Vec3 p,const Sample& reference) const {
    if(!enabled_ || reference.ocean || reference.freshwater) return 0;
    const auto s=sample(p);return s.available ? mix(referenceSnowDepth(reference),s.depth,s.weight) : referenceSnowDepth(reference);
}
double Snow::walkingDepth(Vec3 p) const {
    const auto s=sample(p);return s.available ? std::max(0.,s.depth-.18)*s.weight : 0;
}
SnowAtmosphere Snow::atmosphere(const World& w,const Weather& weather,Vec3 p,double elevation) const {
    SnowAtmosphere a;if(!enabled_) return a;
    const auto air=weather.sample(p);
    a.forcing={clamp(air.temperature+.0065*(weather.layerAltitude()-elevation),180.,330.),clamp(air.humidity,0.,1.),air.rainRate/1000,
        {dot(air.velocity,east_)*.7,dot(air.velocity,north_)*.7}};
    if(experiment_!=SnowExperiment::Natural) {
        constexpr std::array<double,6> temperature{0,261.15,272.15,264.15,261.15,280.15};
        constexpr std::array<double,6> precipitation{0,1.5,4,6,0,0},wind{0,3,2,24,18,3};
        const auto i=static_cast<std::size_t>(experiment_);
        a.forcing={temperature[i],experiment_==SnowExperiment::Drifting ? .7 : .98,weather.cloudsEnabled() ? precipitation[i]/3.6e6 : 0,
            w.modules().enabled(Module::Wind) ? Wind2{wind[i]*.94,wind[i]*.342} : Wind2{}};
    }
    a.fraction=snowFraction(a.forcing.temperature,a.forcing.humidity);
    const double speed=std::hypot(a.forcing.wind.east,a.forcing.wind.north);
    a.large=smoothstep(260.15,273.15,a.forcing.temperature)*(1-.8*smoothstep(8.,22.,speed));
    a.fallingMmH=a.forcing.precipitation*a.fraction*3.6e6;
    const auto s=sample(p);a.blowing=s.available && speed>6 ? clamp(s.airborne*60000,0.,1.) : 0;
    a.visibility=std::min(100000.,3.912/std::max(1e-9,.0015*a.fallingMmH+.055*a.blowing));
    return a;
}
void Snow::step(const World& w,const Weather& weather,double seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>120) throw std::invalid_argument("Invalid snow interval");
    if(!active() || !initialized_ || seconds==0) return;
    const auto p=directionAt(cellX(resolution/2),cellZ(resolution/2));
    const auto a=atmosphere(w,weather,p,grid_.cells[resolution*resolution/2+resolution/2].bed);
    liquidRate_=a.forcing.precipitation*(1-a.fraction);
    fraction_=a.fraction;
    const auto change=grid_.advance(seconds,a.forcing);
    totals_.snowfallM3+=change.snowfallM3;totals_.meltM3+=change.meltM3;totals_.sublimatedM3+=change.sublimatedM3;
    totals_.exportM3+=change.exportM3;totals_.redistributedM3+=change.redistributedM3;++steps_;
    bool changed=false;for(std::size_t i=0;i<grid_.cells.size();++i) if(std::abs(grid_.cells[i].depth-displayDepth_[i])>.025) changed=true;
    if(changed) {for(std::size_t i=0;i<grid_.cells.size();++i) displayDepth_[i]=grid_.cells[i].depth;++revision_;}
}
void Snow::animate(const World& w,const Weather& weather,Vec3 p,double seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>86400) throw std::invalid_argument("Invalid snow animation interval");
    solar_+=seconds;
    if(!active()) return;
    const auto s=sample(p);const auto a=atmosphere(w,weather,p,s.available ? s.bed : w.surface(p));
    time_+=seconds;driftX_+=a.forcing.wind.east*seconds;driftZ_+=a.forcing.wind.north*seconds;
}
std::vector<SnowParticle> Snow::particles(const World& w,const Weather& weather,Vec3 observer,double altitude) const {
    std::vector<SnowParticle> result;if(!active() || !initialized_) return result;
    const auto ground=sample(observer);if(!ground.available || altitude-ground.bed>100) return result;
    const auto a=atmosphere(w,weather,observer,ground.bed);
    const unsigned falling=static_cast<unsigned>(std::min(1600.,a.fallingMmH*250)),blowing=static_cast<unsigned>(a.blowing*1000);
    if(falling+blowing==0) return result;
    result.reserve(falling+blowing);
    const double projection=dot(observer,anchor_),cx=dot(observer,east_)*config_.radius/projection,cz=dot(observer,north_)*config_.radius/projection;
    for(unsigned i=0;i<falling+blowing;++i) {
        const auto id=hash64(config_.seed^hash64(i+0x5a018ULL));const bool blown=i>=falling,large=!blown && randomUnit(id+2)<a.large;
        const double response=blown ? 1 : large ? .75 : .95;
        const double x=cx+periodic(randomUnit(id)*128+driftX_*response-cx,128)-64;
        const double z=cz+periodic(randomUnit(id+1)*128+driftZ_*response-cz,128)-64;
        const auto s=sampleLocal(x,z);if(!s.available) continue;
        const double terminal=large ? .65+.55*randomUnit(id+3) : .25+.4*randomUnit(id+3);
        const double height=blown ? s.bed+s.depth+.08+2.4*std::pow(randomUnit(id+4),2)+.12*std::sin(time_*2+randomUnit(id+5)*tau) :
            std::floor(altitude/80)*80-20+periodic(randomUnit(id+4)*120-time_*terminal,120);
        if(height<s.bed+s.depth+.02 || height>altitude+65) continue;
        const double flutter=blown ? 0 : (large ? .3 : .08)*std::sin(time_*(large ? 1.4 : 2.8)+randomUnit(id+5)*tau);
        result.push_back({directionAt(x+flutter,z+flutter*.4),height,large ? .02+.018*randomUnit(id+6) : .006,
            (blown ? .42 : .85)*(1-smoothstep(45.,64.,std::hypot(x-cx,z-cz))),large,blown});
    }
    return result;
}
SnowBudget Snow::budget() const {auto b=totals_;b.storedM3=grid_.waterVolume();return b;}
Vec3 findSnowfield(const World& w,Vec3 near,double seconds) {
    if(!w.modules().enabled(Module::Snow)) throw std::runtime_error("Enable Snow, Terrain and Climate to visit snow");
    Vec3 best{};double score=1e30;
    for(int i=0;i<2200;++i) {
        const auto p=direction(std::asin(1-2*(i+.5)/2200.),i*pi*(3-std::sqrt(5.)));
        const auto s=w.sample(p,seconds);if(s.ocean || s.freshwater || s.elevation<30 || s.elevation>2600 || s.temperature<252 || s.temperature>270 || s.precipitation<350) continue;
        double slope=0;for(const auto axis:{east(p),north(p)}) slope=std::max(slope,std::abs(w.elevation(normalized(p+axis*(60/w.config().radius)))-s.elevation)/60);
        if(slope>.3 || w.caveSite(p)) continue;
        const double value=length(p-near)+std::abs(s.temperature-266)*.025+slope*3;
        if(value<score) {score=value;best=p;}
    }
    if(length(best)<.5) throw std::runtime_error("No suitable cold snowfield was found");
    return best;
}
} // namespace planet

#include "planet/global_weather.hpp"
#include <stdexcept>

namespace planet {
std::string_view stormName(StormKind kind) {
    switch(kind) { case StormKind::Hurricane:return "Tropical cyclone";case StormKind::Thunderstorm:return "Thunderstorm";default:return "Extratropical cyclone"; }
}
std::optional<StormKind> stormKind(std::string_view s) {
    if(s=="cyclone") return StormKind::Cyclone;
    if(s=="hurricane") return StormKind::Hurricane;
    if(s=="thunderstorm") return StormKind::Thunderstorm;
    return {};
}
double cycloneWind(double r,double peak,double deficit,double f,double shape) {
    if(!std::isfinite(r) || r<0 || !std::isfinite(peak) || peak<=0 || !std::isfinite(deficit) || deficit<0 ||
       !std::isfinite(f) || !std::isfinite(shape) || shape<=0) throw std::invalid_argument("Invalid cyclone profile");
    if(r<1 || deficit==0) return 0;
    const double q=std::pow(peak/r,shape),rotation=std::abs(f)*r/2;
    if(q>700) return 0;
    const double pressureTerm=shape*deficit/1.15*q*std::exp(-q);
    return pressureTerm/(std::sqrt(pressureTerm+rotation*rotation)+rotation+1e-30);
}
double tropicalSupport(double t,double lat,bool ocean,double humidity) {
    if(!std::isfinite(t) || !std::isfinite(lat) || !std::isfinite(humidity)) throw std::invalid_argument("Invalid cyclone environment");
    if(!ocean || std::abs(lat)<radians(5) || std::abs(lat)>radians(35)) return 0;
    return smoothstep(299.65,302.65,t)*smoothstep(.55,.85,humidity);
}
double oceanTemperature(const Config& config,Vec3 p,double mean) {
    return mean+4*std::sin(2*longitude(p)+tau*randomUnit(config.seed+715))*std::pow(std::cos(latitude(p)),4);
}
double sphericalCellArea(int row,int columns,int rows,double radius) {
    if(columns<4 || rows<2 || row<0 || row>=rows || !std::isfinite(radius) || radius<=0) throw std::invalid_argument("Invalid spherical grid");
    const double a=-pi/2+pi*row/rows,b=-pi/2+pi*(row+1)/rows;
    return radius*radius*tau/columns*(std::sin(b)-std::sin(a));
}
std::vector<double> transportSphere(std::span<const double> water,std::span<const Wind2> wind,int nx,int ny,double radius,double seconds) {
    if(nx<4 || nx>256 || ny<2 || ny>128 || water.size()!=static_cast<std::size_t>(nx*ny) || wind.size()!=water.size() ||
       !std::isfinite(radius) || radius<=0 || !std::isfinite(seconds) || seconds<0 || seconds>3600) throw std::invalid_argument("Invalid spherical transport");
    for(std::size_t i=0;i<water.size();++i) if(!std::isfinite(water[i]) || water[i]<0 || !std::isfinite(wind[i].east) || !std::isfinite(wind[i].north))
        throw std::invalid_argument("Nonfinite global wind or water");
    const auto index=[&](int x,int y){return static_cast<std::size_t>(y*nx+(x+nx)%nx);};
    std::vector<double> area(water.size()),outgoing(water.size()),next(water.begin(),water.end());
    struct Face { std::size_t a,b;double flow; };std::vector<Face> faces;
    for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
        const auto i=index(x,y),j=index(x+1,y);area[i]=sphericalCellArea(y,nx,ny,radius);
        faces.push_back({i,j,(wind[i].east+wind[j].east)*.5*radius*pi/ny});
        if(y+1<ny) { const auto n=index(x,y+1);faces.push_back({i,n,(wind[i].north+wind[n].north)*.5*radius*tau/nx*std::cos(-pi/2+pi*(y+1)/ny)}); }
    }
    for(const auto& f:faces) outgoing[f.flow>=0 ? f.a : f.b]+=std::abs(f.flow);
    double rate=0;for(std::size_t i=0;i<area.size();++i) rate=std::max(rate,outgoing[i]/area[i]);
    const double step=std::min(seconds,.8/std::max(rate,1e-20));
    if(seconds>0 && seconds/std::max(step,1e-30)>10000) throw std::invalid_argument("Spherical transport requires too many substeps");
    for(double left=seconds;left>1e-10;) {
        const double dt=std::min(step,left);const auto old=next;
        for(const auto& f:faces) {
            const double mass=f.flow*dt*old[f.flow>=0 ? f.a : f.b];
            next[f.a]-=mass/area[f.a];next[f.b]+=mass/area[f.b];
        }
        for(auto& v:next) v=std::max(0.0,v);
        left-=dt;
    }
    return next;
}
Vec3 GlobalWeather::cellDirection(int x,int y) const { return direction(-pi/2+pi*(y+.5)/rows,-pi+tau*(x+.5)/columns); }
void GlobalWeather::background(const World& world) {
    habitat_.clear();habitat_.reserve(columns*rows);
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        const auto p=cellDirection(x,y);const auto s=world.climatology(p);
        const double t=std::isfinite(s.temperature) ? (s.ocean ? oceanTemperature(config_,p,s.temperature) : s.temperature) : 288.15;
        const auto air=standardAtmosphere(1800+std::max(0.0,s.elevation),clamp(t+.0065*std::max(0.0,s.elevation),180.0,340.0),101325,config_.radius,config_.gravity);
        habitat_.push_back({p,air.temperature,air.pressure,air.density*800,s.ocean ? 1.0 : clamp(s.precipitation/1500,0.0,1.0),sphericalCellArea(y,columns,rows,config_.radius),s.ocean});
    }
}
std::optional<Storm> GlobalWeather::seedStorm(const World& world,StormKind kind,std::uint64_t id,std::uint64_t generation) const {
    if(kind!=StormKind::Cyclone && !world.modules().enabled(Module::Climate)) return {};
    const auto salt=hash64(config_.seed^id^hash64(generation+7183));
    for(unsigned i=0;i<1400;++i) {
        const auto h=hash64(salt+i);const double sign=randomUnit(h)<.5 ? -1.0 : 1.0;
        const double lat=kind==StormKind::Cyclone ? sign*radians(30+30*randomUnit(h+1)) : sign*radians(6+27*randomUnit(h+1));
        const auto p=direction(lat,(randomUnit(h+2)-.5)*tau);const auto s=world.climatology(p);
        if(kind==StormKind::Hurricane) {
            if(tropicalSupport(oceanTemperature(config_,p,s.temperature),lat,s.ocean,.85)<.35) continue;
            bool ocean=true;for(int k=0;k<4;++k) if(world.elevation(normalized(p+(east(p)*std::cos(k*pi/2)+north(p)*std::sin(k*pi/2))*(150000/config_.radius)))>config_.seaLevel) ocean=false;
            if(!ocean) continue;
        }
        if(kind==StormKind::Thunderstorm && (s.ocean || s.elevation>1800 || s.temperature<291 || s.precipitation<700 || world.caveSite(p))) continue;
        const double lifetime=kind==StormKind::Thunderstorm ? 4*3600 : (kind==StormKind::Hurricane ? 6*86400 : 4*86400);
        const double radius=kind==StormKind::Thunderstorm ? 14000 : (kind==StormKind::Hurricane ? 55000 : 240000);
        const double deficit=kind==StormKind::Thunderstorm ? 250 : (kind==StormKind::Hurricane ?
            8500*tropicalSupport(oceanTemperature(config_,p,s.temperature),lat,s.ocean,.85) : 2800);
        return Storm{id,generation,kind,p,generation==0 ? lifetime*(.20+.35*randomUnit(h+3)) : 0,lifetime,radius,deficit*(generation==0 ? .85 : .05)};
    }
    return {};
}
GlobalWeather::GlobalWeather(const World& world,double seconds,double hours):config_(world.config()) {
    setSolarClock(seconds);background(world);enabled_=world.modules().enabled(Module::GlobalWeather);cloudsEnabled_=world.modules().enabled(Module::Clouds);
    for(unsigned i=0;i<12;++i) if(auto storm=seedStorm(world,static_cast<StormKind>(i%3),hash64(config_.seed+i+3918),0)) storms_.push_back(*storm);
    const Noise noise(config_.seed+78117);cells_.resize(habitat_.size());
    for(std::size_t i=0;i<cells_.size();++i) {
        const auto& h=habitat_[i];auto& c=cells_[i];c.temperature=h.temperature;
        c.vapour=h.airMass*saturationMixingRatio(c.temperature,h.pressure)*clamp(.65+.28*h.wetness+.25*noise.value(h.direction*12),.3,1.1);
        c.cloud=.15+.35*std::max(0.0,noise.value(h.direction*18));
        equilibrateMoisture(c.vapour,c.cloud,c.temperature,h.airMass,h.pressure);
    }
    initialWater_=waterMass();advance(world,0);
    if(!std::isfinite(hours) || hours<0 || hours>240) throw std::invalid_argument("Global preview must be 0..240 hours");
    for(double left=hours*3600;left>0;) {const double dt=std::min(left,86400.0);advance(world,dt);advanceEffects(dt);left-=dt;}
}
void GlobalWeather::setSolarClock(double seconds) {
    if(!std::isfinite(seconds) || std::abs(seconds)>1e12) throw std::invalid_argument("Invalid global solar clock");
    solarTime_=seconds;
}
void GlobalWeather::advanceEffects(double seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>86400) throw std::invalid_argument("Invalid weather effects clock");
    if(enabled_) effects_+=seconds;
}
GlobalSample GlobalWeather::forcing(Vec3 p) const {
    GlobalSample s;s.available=enabled_;if(!enabled_) return s;
    const double lat=latitude(p),c=std::cos(lat);
    s.velocity=east(p)*(-7*std::cos(4*lat)*c)+north(p)*(-2*std::sin(2*lat));
    s.pressure=101325+650*std::cos(6*lat);
    for(const auto& storm:storms_) {
        const double cosine=clamp(dot(p,storm.center),-1.0,1.0);
        if(cosine<std::cos(std::min(pi,storm.radius*14/config_.radius))) continue;
        const double r=std::acos(cosine)*config_.radius;
        const double envelope=1-smoothstep(storm.radius*9,storm.radius*14,r);
        const auto outward=normalized(p*cosine-storm.center);
        const double f=2*tau/config_.day*std::sin(latitude(storm.center));
        const double q=std::pow(storm.radius/std::max(1.0,r),1.4);
        const double profile=std::exp(-.5*std::pow(r/(storm.radius*2.8),2));
        const double peak=storm.kind==StormKind::Hurricane ? 7600 : (storm.kind==StormKind::Cyclone ? 2800 : 250);
        const double strength=clamp(storm.deficit/peak,0.0,1.4);
        double activity=profile*strength;
        if(storm.kind==StormKind::Thunderstorm) {
            s.velocity+=outward*(16*strength*(r/storm.radius)*std::exp(-r/storm.radius));
            s.pressure+=storm.deficit*profile; s.convection=std::max(s.convection,activity);
        } else {
            const double speed=cycloneWind(r,storm.radius,storm.deficit,f)*envelope;
            s.velocity+=(cross(p,outward)*std::copysign(1.0,f)-outward*.16)*speed;
            s.pressure-=storm.deficit*(1-std::exp(-std::min(q,700.0)))*envelope;
            if(storm.kind==StormKind::Hurricane) activity*=smoothstep(storm.radius*.35,storm.radius*.85,r);
            const Vec3 offset=p-storm.center*cosine;
            const double angle=std::atan2(dot(offset,north(storm.center)),dot(offset,east(storm.center)));
            activity*=.75+.25*std::cos(3*angle-3*std::log1p(r/storm.radius));
            if(storm.kind==StormKind::Hurricane) s.convection=std::max(s.convection,activity*.55);
        }
        if(activity>s.severity) {s.severity=activity;s.kind=storm.kind;}
    }
    s.velocity=s.velocity-p*dot(s.velocity,p);
    if(length(s.velocity)>70) s.velocity=s.velocity*(70/length(s.velocity));
    s.pressure=clamp(s.pressure,85000.0,104000.0);
    return s;
}
GlobalSample GlobalWeather::sample(Vec3 p) const {
    p=normalized(p);auto s=forcing(p);if(!s.available) return s;
    const double gx=(longitude(p)+pi)/tau*columns-.5,gy=(latitude(p)+pi/2)/pi*rows-.5;
    const int x=static_cast<int>(std::floor(gx)),y=static_cast<int>(std::floor(gy));
    const double tx=gx-std::floor(gx),ty=gy-std::floor(gy);
    double t=0,rh=0,cloud=0,rain=0;
    for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
        const auto i=static_cast<std::size_t>(clamp(y+dy,0,rows-1)*columns+((x+dx)%columns+columns)%columns);
        const double w=(dx ? tx : 1-tx)*(dy ? ty : 1-ty);const auto& c=cells_[i];const auto& h=habitat_[i];
        t+=(c.temperature+11.7)*w;rh+=c.vapour/(h.airMass*saturationMixingRatio(c.temperature,h.pressure))*w;cloud+=c.cloud*w;rain+=c.rainRate*w;
    }
    // All longitudes meet at a pole. Blend the final half-cell to its ring mean.
    const double polar=clamp((std::abs(latitude(p))-(pi/2-pi/(2*rows)))/(pi/(2*rows)),0.0,1.0);
    if(polar>0) {
        double pt=0,prh=0,pc=0,pr=0;const int row=p.y>0 ? rows-1 : 0;
        for(int j=0;j<columns;++j) {
            const auto i=static_cast<std::size_t>(row*columns+j);const auto& c=cells_[i];const auto& h=habitat_[i];
            pt+=c.temperature+11.7;prh+=c.vapour/(h.airMass*saturationMixingRatio(c.temperature,h.pressure));pc+=c.cloud;pr+=c.rainRate;
        }
        t=mix(t,pt/columns,polar);rh=mix(rh,prh/columns,polar);cloud=mix(cloud,pc/columns,polar);rain=mix(rain,pr/columns,polar);
    }
    s.temperature=t;s.humidity=cloudsEnabled_ ? clamp(rh+.18*s.severity,.1,1.05) : 0;
    // Subgrid cloud is a nesting target, not extra mass in the global ledger.
    s.cloud=cloudsEnabled_ ? cloud+3*s.severity : 0;s.rainRate=cloudsEnabled_ ? rain : 0;
    if(!cloudsEnabled_) s.convection=0;
    return s;
}
double GlobalWeather::waterMass() const { long double total=0;for(std::size_t i=0;i<cells_.size();++i) total+=(cells_[i].vapour+cells_[i].cloud)*habitat_[i].area;return static_cast<double>(total); }
WaterBudget GlobalWeather::budget() const {return {initialWater_,waterMass(),evaporation_,precipitation_,0};}
void GlobalWeather::advance(const World& world,double seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>86400) throw std::invalid_argument("Global weather timestep must be 0..86400 s");
    if(world.config().seed!=config_.seed || world.config().radius!=config_.radius) throw std::invalid_argument("Reset global weather after changing planet");
    const bool wasEnabled=enabled_;
    enabled_=world.modules().enabled(Module::GlobalWeather);cloudsEnabled_=world.modules().enabled(Module::Clouds);
    unsigned mask=0;for(const auto m:{Module::Terrain,Module::Ocean,Module::Landforms,Module::Climate}) if(world.modules().enabled(m)) mask|=1U<<static_cast<unsigned>(m);
    if(mask!=geometry_) {background(world);geometry_=mask;}
    if(!enabled_) return;
    if(!wasEnabled) for(unsigned i=0;i<12;++i) {
        const auto id=hash64(config_.seed+i+3918);
        if(std::none_of(storms_.begin(),storms_.end(),[&](const Storm& storm){return storm.id==id;}))
            if(const auto fresh=seedStorm(world,static_cast<StormKind>(i%3),id,0)) storms_.push_back(*fresh);
    }
    pending_+=seconds;
    while(pending_+1e-9>=fixedStep) {step(world);elapsed_+=fixedStep;solarTime_+=fixedStep;pending_=std::max(0.0,pending_-fixedStep);++steps_;}
}
void GlobalWeather::step(const World& world) {
    for(auto& storm:storms_) {
        const double lat=latitude(storm.center);
        const auto drift=east(storm.center)*(-9*std::cos(3*lat))+north(storm.center)*(1.5*std::copysign(1.0,lat));
        const auto axis=normalized(cross(storm.center,drift));storm.center=normalized(rotate(storm.center,axis,length(drift)*fixedStep/config_.radius));
        storm.age+=fixedStep;
        const auto s=world.climatology(storm.center);
        const double life=std::max(0.0,std::sin(pi*clamp(storm.age/storm.lifetime,0.0,1.0)));
        double target=2800*life,relax=12*3600;
        if(storm.kind==StormKind::Hurricane) {target=8500*tropicalSupport(oceanTemperature(config_,storm.center,s.temperature),latitude(storm.center),s.ocean,.85)*life;relax=s.ocean ? 18*3600 : 5*3600;}
        if(storm.kind==StormKind::Thunderstorm) {target=s.ocean ? 0 : 300*life;relax=1800;}
        storm.deficit=mix(storm.deficit,target,1-std::exp(-fixedStep/relax));
        if(storm.age>storm.lifetime+(storm.kind==StormKind::Thunderstorm ? 7200 : 86400)) {
            if(const auto fresh=seedStorm(world,storm.kind,storm.id,storm.generation+1)) storm=*fresh;
        }
    }
    std::vector<Wind2> wind;std::vector<double> vapour,cloud;wind.reserve(cells_.size());vapour.reserve(cells_.size());cloud.reserve(cells_.size());
    for(std::size_t i=0;i<cells_.size();++i) {
        const auto& h=habitat_[i];const auto v=forcing(h.direction).velocity;
        wind.push_back({dot(v,east(h.direction)),dot(v,north(h.direction))});vapour.push_back(cells_[i].vapour);cloud.push_back(cells_[i].cloud);
    }
    if(cloudsEnabled_) {vapour=transportSphere(vapour,wind,columns,rows,config_.radius,fixedStep);cloud=transportSphere(cloud,wind,columns,rows,config_.radius,fixedStep);}
    const auto sun=orbit(config_,solarTime_);
    for(std::size_t i=0;i<cells_.size();++i) {
        auto& c=cells_[i];const auto& h=habitat_[i];const auto synoptic=forcing(h.direction);
        const double solar=(h.ocean ? 1.0 : 4.0)*dot(h.direction,sun.sunDirection);
        c.temperature=mix(c.temperature,h.temperature+solar-2*synoptic.severity,1-std::exp(-fixedStep/21600));c.rainRate=0;
        if(!cloudsEnabled_) continue;
        const double saturation=h.airMass*saturationMixingRatio(c.temperature,h.pressure);
        const double input=3e-5*h.wetness*(.5+length(synoptic.velocity)/10)*std::max(0.0,1-vapour[i]/saturation)*fixedStep;
        c.vapour=vapour[i]+input;c.cloud=cloud[i];evaporation_+=input*h.area;
        equilibrateMoisture(c.vapour,c.cloud,c.temperature,h.airMass,h.pressure);
        const double rain=std::max(0.0,c.cloud-.25)*(1-std::exp(-fixedStep/1800));
        c.cloud-=rain;c.rainRate=rain/fixedStep;precipitation_+=rain*h.area;
    }
}
std::optional<Storm> GlobalWeather::strongest(StormKind kind) const {
    std::optional<Storm> found;
    for(const auto& s:storms_) if(s.kind==kind && s.deficit>1 && (!found || s.deficit>found->deficit)) found=s;
    return found;
}
Vec3 GlobalWeather::observation(StormKind kind) const {
    const auto s=strongest(kind);if(!s) return {1,0,0};
    return kind==StormKind::Thunderstorm ? s->center : normalized(s->center+east(s->center)*(s->radius/config_.radius));
}
double GlobalWeather::flash(Vec3 p) const {
    const double phase=std::fmod(effects_,12.0);if(phase>=.35) return 0;
    double intensity=0;
    for(const auto& strike:lightning()) {
        const double distance=length(normalized(p)-strike.direction)*config_.radius;
        if(distance<25000) intensity=std::max(intensity,strike.strength*std::exp(-.5*std::pow(distance/39200,2)));
    }
    return (1-phase/.35)*intensity;
}
std::vector<LightningStrike> GlobalWeather::lightning() const {
    std::vector<LightningStrike> strikes;
    if(!enabled_ || !cloudsEnabled_) return strikes;
    const auto cycle=static_cast<std::uint64_t>(effects_/12);
    for(const auto& storm:storms_) if(storm.kind==StormKind::Thunderstorm && storm.deficit>50)
        strikes.push_back({storm.id,storm.generation,cycle,normalized(storm.center+east(storm.center)*(700/config_.radius)),
            static_cast<double>(cycle)*12,clamp(storm.deficit/250,0.0,1.4)});
    return strikes;
}
} // namespace planet

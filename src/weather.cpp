#include "planet/weather.hpp"
#include <complex>
#include <numeric>
#include <stdexcept>

namespace planet {
namespace {
constexpr double latentHeat=2.5e6, specificHeat=1004;
int wrap(int i,int n) { return (i%n+n)%n; }
double wrapCoordinate(double value) {
    return value-Weather::extent*std::floor((value+Weather::extent/2)/Weather::extent);
}
WeatherCell interpolateCells(std::span<const WeatherCell> values,double x,double z) {
    const double gx=(x+Weather::extent/2)/Weather::spacing-0.5;
    const double gz=(z+Weather::extent/2)/Weather::spacing-0.5;
    const int ix=static_cast<int>(std::floor(gx)),iz=static_cast<int>(std::floor(gz));
    const double tx=gx-std::floor(gx),tz=gz-std::floor(gz);
    WeatherCell result{};
    for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
        const auto& c=values[static_cast<std::size_t>(wrap(iz+dz,Weather::resolution)*Weather::resolution+wrap(ix+dx,Weather::resolution))];
        const double w=(dx ? tx : 1-tx)*(dz ? tz : 1-tz);
        result.vapour+=c.vapour*w; result.cloud+=c.cloud*w; result.temperature+=c.temperature*w;
        result.wind.east+=c.wind.east*w; result.wind.north+=c.wind.north*w;
        result.uplift+=c.uplift*w; result.rainRate+=c.rainRate*w;
    }
    return result;
}
void fft(std::span<std::complex<double>> values,bool inverse) {
    const auto n=values.size();
    for(std::size_t i=1,j=0;i<n;++i) {
        std::size_t bit=n>>1;
        while(j&bit) { j^=bit; bit>>=1; }
        j^=bit; if(i<j) std::swap(values[i],values[j]);
    }
    for(std::size_t block=2;block<=n;block*=2) {
        const double angle=(inverse ? tau : -tau)/static_cast<double>(block);
        const std::complex<double> root{std::cos(angle),std::sin(angle)};
        for(std::size_t start=0;start<n;start+=block) {
            std::complex<double> weight{1,0};
            for(std::size_t j=0;j<block/2;++j) {
                const auto a=values[start+j],b=values[start+j+block/2]*weight;
                values[start+j]=a+b; values[start+j+block/2]=a-b; weight*=root;
            }
        }
    }
    if(inverse) for(auto& value:values) value/=static_cast<double>(n);
}
}
void projectWind(std::span<Wind2> faces,int cells,double spacing) {
    if(cells<2 || cells>128 || (cells&(cells-1)) || faces.size()!=static_cast<std::size_t>(cells*cells) ||
       !std::isfinite(spacing) || spacing<=0) throw std::invalid_argument("Projection requires a finite power-of-two grid");
    for(const auto& v:faces) if(!std::isfinite(v.east) || !std::isfinite(v.north)) throw std::invalid_argument("Nonfinite projection wind");
    const auto index=[&](int x,int z) { return static_cast<std::size_t>(wrap(z,cells)*cells+wrap(x,cells)); };
    std::vector<std::complex<double>> potential(faces.size()),line(static_cast<std::size_t>(cells));
    for(int z=0;z<cells;++z) for(int x=0;x<cells;++x) {
        const auto i=index(x,z);
        potential[i]=(faces[i].east-faces[index(x-1,z)].east+faces[i].north-faces[index(x,z-1)].north)/spacing;
    }
    const auto transform=[&](bool inverse) {
        for(int z=0;z<cells;++z) fft(std::span(potential).subspan(static_cast<std::size_t>(z*cells),static_cast<std::size_t>(cells)),inverse);
        for(int x=0;x<cells;++x) {
            for(int z=0;z<cells;++z) line[static_cast<std::size_t>(z)]=potential[index(x,z)];
            fft(line,inverse);
            for(int z=0;z<cells;++z) potential[index(x,z)]=line[static_cast<std::size_t>(z)];
        }
    };
    transform(false);
    for(int z=0;z<cells;++z) for(int x=0;x<cells;++x) {
        const double eigenvalue=-4*(std::pow(std::sin(pi*x/cells),2)+std::pow(std::sin(pi*z/cells),2))/(spacing*spacing);
        potential[index(x,z)]=eigenvalue==0 ? 0 : potential[index(x,z)]/eigenvalue;
    }
    transform(true);
    for(int z=0;z<cells;++z) for(int x=0;x<cells;++x) {
        const auto i=index(x,z);
        faces[i].east-=(potential[index(x+1,z)].real()-potential[i].real())/spacing;
        faces[i].north-=(potential[index(x,z+1)].real()-potential[i].real())/spacing;
    }
}
double saturationPressure(double temperature) {
    if(!std::isfinite(temperature) || temperature<150 || temperature>370)
        throw std::invalid_argument("Saturation temperature outside [150,370] K");
    const double c=temperature-273.15;
    return 610.78*std::exp(17.2694*c/(c+237.3));
}
double saturationMixingRatio(double temperature,double pressure) {
    if(!std::isfinite(pressure) || pressure<1000 || pressure>200000)
        throw std::invalid_argument("Invalid moist-layer pressure");
    const double e=std::min(saturationPressure(temperature),0.9*pressure);
    return 0.622*e/(pressure-e);
}
void equilibrateMoisture(double& vapour,double& cloud,double& temperature,double airMass,double pressure) {
    if(!std::isfinite(vapour) || !std::isfinite(cloud) || vapour<0 || cloud<0 ||
       !std::isfinite(airMass) || airMass<=0)
        throw std::invalid_argument("Invalid water or dry-air column");
    const double coefficient=latentHeat/(specificHeat*airMass);
    const auto excess=[&](double condensed) {
        return vapour-condensed-airMass*saturationMixingRatio(clamp(temperature+coefficient*condensed,150.0,370.0),pressure);
    };
    (void)saturationPressure(temperature);
    double low=-cloud,high=vapour;
    double change=0;
    if(excess(low)<=0) change=low;
    else for(int i=0;i<42;++i) {
        const double error=excess(change);
        if(std::abs(error)<1e-12*std::max(1.0,vapour+cloud)) break;
        if(error>0) low=change; else high=change;
        const double t=clamp(temperature+coefficient*change,150.0,370.0),c=t-273.15;
        const double e=saturationPressure(t);
        const double derivative=e<pressure*0.9 ? 0.622*pressure*e*17.2694*237.3/
                                 (std::pow(c+237.3,2)*std::pow(pressure-e,2)) : 0;
        const double next=change+error/(1+airMass*coefficient*derivative);
        change=next>low && next<high ? next : (low+high)/2;
    }
    const double total=vapour+cloud;
    cloud=clamp(cloud+change,0.0,total); vapour=total-cloud;
    temperature+=coefficient*change;
}
std::vector<double> transportWater(std::span<const double> water,std::span<const Wind2> wind,
                                 int cells,double spacing,double seconds,bool faceCentered) {
    if(cells<2 || cells>512 || water.size()!=static_cast<std::size_t>(cells*cells) || wind.size()!=water.size() ||
       !std::isfinite(spacing) || spacing<=0 || !std::isfinite(seconds) || seconds<0)
        throw std::invalid_argument("Invalid transport grid or timestep");
    const auto index=[&](int x,int z) { return static_cast<std::size_t>(wrap(z,cells)*cells+wrap(x,cells)); };
    const double ratio=seconds/spacing;
    if(!std::isfinite(ratio)) throw std::invalid_argument("Unbounded transport timestep");
    for(std::size_t i=0;i<water.size();++i)
        if(!std::isfinite(water[i]) || water[i]<0 || !std::isfinite(wind[i].east) || !std::isfinite(wind[i].north))
            throw std::invalid_argument("Nonfinite wind or negative water");
    for(int z=0;z<cells;++z) for(int x=0;x<cells;++x) {
        const auto i=index(x,z);
        const double east=faceCentered ? wind[i].east : (wind[i].east+wind[index(x+1,z)].east)/2;
        const double west=faceCentered ? wind[index(x-1,z)].east : (wind[i].east+wind[index(x-1,z)].east)/2;
        const double north=faceCentered ? wind[i].north : (wind[i].north+wind[index(x,z+1)].north)/2;
        const double south=faceCentered ? wind[index(x,z-1)].north : (wind[i].north+wind[index(x,z-1)].north)/2;
        const double outgoing=std::max(0.0,east)+std::max(0.0,-west)+std::max(0.0,north)+std::max(0.0,-south);
        if(outgoing*ratio>1+1e-12) throw std::invalid_argument("Transport violates the outgoing CFL limit");
    }
    std::vector<double> result(water.begin(),water.end());
    const auto flux=[&](std::size_t a,std::size_t b,double speed) {
        const double transferred=speed*ratio*(speed>=0 ? water[a] : water[b]);
        result[a]-=transferred; result[b]+=transferred;
    };
    for(int z=0;z<cells;++z) for(int x=0;x<cells;++x) {
        const auto i=index(x,z),e=index(x+1,z),n=index(x,z+1);
        flux(i,e,faceCentered ? wind[i].east : (wind[i].east+wind[e].east)/2);
        flux(i,n,faceCentered ? wind[i].north : (wind[i].north+wind[n].north)/2);
    }
    for(auto& value:result) value=std::max(0.0,value); // Roundoff only; CFL guarantees positivity.
    return result;
}
Weather::Weather(const World& world,Vec3 observer,double seconds) : config_(world.config()) {
    setClock(seconds);
    anchor_=normalized(observer); east_=east(anchor_); north_=north(anchor_);
    background(world);
    const Noise noise(config_.seed^0x76ea19ULL);
    cells_.resize(resolution*resolution);
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const auto i=static_cast<std::size_t>(z*resolution+x);
        const double px=(x+0.5)*spacing-extent/2,pz=(z+0.5)*spacing-extent/2;
        const Vec3 p=directionAt(px,pz)*config_.radius;
        const auto& b=background_[i]; auto& c=cells_[i];
        c.temperature=b.temperature;
        const double humidity=clamp(0.78+0.16*b.wetness+0.46*noise.value(p/4200)+0.12*noise.value(p/1500),0.30,1.35);
        c.vapour=humidity*b.airMass*saturationMixingRatio(c.temperature,b.pressure);
        equilibrateMoisture(c.vapour,c.cloud,c.temperature,b.airMass,b.pressure);
        c.wind={-6*std::cos(3*latitude(anchor_))+1.5*noise.value(p/7000),-1.5*std::sin(2*latitude(anchor_))};
    }
    seedParticles(); initialWater_=waterMass();
    advance(world,observer,0);
}
void Weather::setClock(double seconds) {
    if(!std::isfinite(seconds) || std::abs(seconds)>1e12) throw std::invalid_argument("Invalid weather clock");
    time_=seconds; pending_=0;
}
Vec3 Weather::directionAt(double x,double z) const {
    return normalized(anchor_+(east_*x+north_*z)/config_.radius);
}
Vec3 Weather::position(double x,double z,double altitude) const { return directionAt(x,z)*(config_.radius+altitude); }
void Weather::background(const World& world) {
    const double centerGround=world.surface(anchor_);
    layerAltitude_=std::max(config_.seaLevel+1800,centerGround+1800);
    annualTemperature_=world.climatology(anchor_).temperature;
    if(!std::isfinite(annualTemperature_)) annualTemperature_=288.15;
    const Noise noise(config_.seed^0x552715abULL);
    background_.resize(resolution*resolution);
    std::vector<Sample> climates(resolution*resolution);
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const auto p=directionAt((x+0.5)*spacing-extent/2,(z+0.5)*spacing-extent/2);
        const auto c=world.climatology(p);
        climates[static_cast<std::size_t>(z*resolution+x)]=c;
        layerAltitude_=std::max(layerAltitude_,(c.ocean ? config_.seaLevel : c.elevation)+600);
    }
    const auto reference=standardAtmosphere(layerAltitude_,clamp(annualTemperature_+0.0065*centerGround,180.0,340.0),
                                           101325,config_.radius,config_.gravity);
    const double airMass=reference.density*layerDepth; // One Boussinesq reference column throughout the domain.
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const Vec3 p=directionAt((x+0.5)*spacing-extent/2,(z+0.5)*spacing-extent/2);
        const auto climate=climates[static_cast<std::size_t>(z*resolution+x)];
        const double ground=climate.ocean ? config_.seaLevel : climate.elevation;
        const double seaTemperature=std::isfinite(climate.temperature) ? clamp(climate.temperature+0.0065*ground,180.0,340.0) : 288.15;
        const auto air=standardAtmosphere(layerAltitude_,seaTemperature,101325,config_.radius,config_.gravity);
        const double edge=std::max(std::abs((x+0.5)*spacing-extent/2),std::abs((z+0.5)*spacing-extent/2));
        const double forcingWeight=1-smoothstep(extent*0.30,extent*0.49,edge);
        background_[static_cast<std::size_t>(z*resolution+x)]={air.temperature,air.pressure,airMass,ground,
            climate.ocean ? 1.0 : clamp(climate.precipitation/1500,0.0,1.0),
            30*noise.value(p*(config_.radius/16000)),18*noise.value(p*(config_.radius/9000)+Vec3{11,37,9}),forcingWeight};
    }
    backgroundModules_=0;
    for(unsigned i=0;i<4;++i) if(world.modules().enabled(static_cast<Module>(i))) backgroundModules_|=1U<<i;
}
void Weather::seedParticles() {
    clouds_.clear(); tracers_.clear();
    for(std::uint64_t i=0;i<2048;++i) {
        const auto id=hash64(config_.seed^i^0xc10dULL);
        clouds_.push_back({(randomUnit(id)-0.5)*extent,(randomUnit(id+1)-0.5)*extent,
                          (randomUnit(id+2)-0.5)*layerDepth,350+350*randomUnit(id+3),id});
    }
    for(std::uint64_t i=0;i<160;++i) {
        const auto id=hash64(config_.seed^i^0xa17ULL);
        const double angle=tau*randomUnit(id),distance=1800*std::sqrt(randomUnit(id+1));
        tracers_.push_back({std::cos(angle)*distance,std::sin(angle)*distance,8+100*randomUnit(id+2),1,id});
    }
}
WeatherCell Weather::interpolate(double x,double z) const { return interpolateCells(cells_,x,z); }
double Weather::groundAt(double x,double z) const {
    const int ix=clamp(static_cast<int>((x+extent/2)/spacing),0,resolution-1);
    const int iz=clamp(static_cast<int>((z+extent/2)/spacing),0,resolution-1);
    return background_[static_cast<std::size_t>(iz*resolution+ix)].terrain;
}
WeatherSample Weather::sampleLocal(double x,double z) const {
    if(!std::isfinite(x) || !std::isfinite(z) || std::abs(x)>=extent/2 || std::abs(z)>=extent/2) return {};
    const auto c=interpolate(x,z);
    const int ix=clamp(static_cast<int>((x+extent/2)/spacing),0,resolution-1);
    const int iz=clamp(static_cast<int>((z+extent/2)/spacing),0,resolution-1);
    const auto& b=background_[static_cast<std::size_t>(iz*resolution+ix)];
    const Wind2 wind=windEnabled_ ? c.wind : Wind2{};
    const Vec3 up=directionAt(x,z);
    Vec3 velocity=east_*wind.east+north_*wind.north;
    velocity=velocity-up*dot(velocity,up);
    return {true,velocity,wind,c.temperature,cloudsEnabled_ ? c.vapour/(b.airMass*saturationMixingRatio(c.temperature,b.pressure)) : 0,
            cloudsEnabled_ ? c.cloud : 0,cloudsEnabled_ ? c.rainRate : 0,b.pressure};
}
WeatherSample Weather::sample(Vec3 p) const {
    p=normalized(p); const double projection=dot(p,anchor_);
    if(projection<=0) return {};
    return sampleLocal(dot(p,east_)*config_.radius/projection,dot(p,north_)*config_.radius/projection);
}
double Weather::waterMass() const {
    double total=0; for(const auto& c:cells_) total+=c.vapour+c.cloud;
    return total*spacing*spacing;
}
WaterBudget Weather::budget() const { return {initialWater_,waterMass(),evaporation_,precipitation_,domainExchange_}; }
void Weather::recenter(const World& world,Vec3 observer) {
    const auto previous=cells_;
    const Vec3 oldAnchor=anchor_,oldEast=east_,oldNorth=north_;
    const double previousMass=waterMass(),oldAltitude=layerAltitude_;
    // A new local domain supplies the entering cells. Carry overlapping column
    // water and rotate wind vectors; account explicitly for the changed domain.
    Weather fresh(world,observer,time_+pending_);
    anchor_=fresh.anchor_; east_=fresh.east_; north_=fresh.north_;
    layerAltitude_=fresh.layerAltitude_; annualTemperature_=fresh.annualTemperature_;
    background_=std::move(fresh.background_); cells_=std::move(fresh.cells_);
    backgroundModules_=fresh.backgroundModules_;
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const Vec3 p=directionAt((x+0.5)*spacing-extent/2,(z+0.5)*spacing-extent/2);
        const double projection=dot(p,oldAnchor);
        if(projection<=0) continue;
        const double px=dot(p,oldEast)*config_.radius/projection,pz=dot(p,oldNorth)*config_.radius/projection;
        if(std::abs(px)>extent/2-spacing || std::abs(pz)>extent/2-spacing) continue;
        auto c=interpolateCells(previous,px,pz);
        const Vec3 velocity=oldEast*c.wind.east+oldNorth*c.wind.north;
        c.wind={dot(velocity,east_),dot(velocity,north_)};
        c.temperature=clamp(c.temperature-0.0065*(layerAltitude_-oldAltitude),200.0,320.0);
        cells_[static_cast<std::size_t>(z*resolution+x)]=c;
    }
    domainExchange_+=waterMass()-previousMass;
    seedParticles();
}
void Weather::advance(const World& world,Vec3 observer,double modelSeconds) {
    if(!std::isfinite(modelSeconds) || modelSeconds<0 || modelSeconds>86400 ||
       (world.modules().enabled(Module::Wind) && modelSeconds>120))
        throw std::invalid_argument("Weather timestep exceeds its supported range");
    if(world.config().seed!=config_.seed || world.config().radius!=config_.radius)
        throw std::invalid_argument("Reset weather after changing the planet");
    windEnabled_=world.modules().enabled(Module::Wind); cloudsEnabled_=world.modules().enabled(Module::Clouds);
    unsigned modules=0;
    for(unsigned i=0;i<4;++i) if(world.modules().enabled(static_cast<Module>(i))) modules|=1U<<i;
    if(modules!=backgroundModules_) background(world);
    if(length(normalized(observer)-anchor_)*config_.radius>5000) recenter(world,observer);
    if(!windEnabled_) { time_+=pending_+modelSeconds; pending_=0; return; }
    pending_+=modelSeconds;
    while(pending_+1e-9>=fixedStep) {
        step(world,observer); time_+=fixedStep; pending_=std::max(0.0,pending_-fixedStep); ++steps_;
    }
}
void Weather::step(const World& world,Vec3 observer) {
    const auto previous=cells_;
    const auto index=[](int x,int z) { return static_cast<std::size_t>(wrap(z,resolution)*resolution+wrap(x,resolution)); };
    const double coriolis=2*(tau/config_.day)*std::sin(latitude(anchor_));
    const double rotation=std::cos(coriolis*fixedStep),deflection=std::sin(coriolis*fixedStep);
    const double phase=tau*std::remainder(time_,21600.0)/21600;
    const Wind2 prevailing{-6*std::cos(3*latitude(anchor_)),-1.5*std::sin(2*latitude(anchor_))};
    const double surfaceTemperature=world.sample(anchor_,time_).temperature;
    const double season=std::isfinite(surfaceTemperature) ? clamp(surfaceTemperature-annualTemperature_,-15.0,15.0) : 0;
    std::vector<double> pressure(cells_.size());
    for(std::size_t i=0;i<cells_.size();++i) {
        const auto& b=background_[i];
        pressure[i]=b.forcingWeight*(b.pressureA*std::cos(phase)+b.pressureB*std::sin(phase)+2*(previous[i].temperature-b.temperature));
    }
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const auto i=index(x,z),e=index(x+1,z),w=index(x-1,z),n=index(x,z+1),s=index(x,z-1);
        const auto& b=background_[i]; auto& c=cells_[i];
        const auto carried=interpolateCells(previous,(x+0.5)*spacing-extent/2-previous[i].wind.east*fixedStep,
                                                      (z+0.5)*spacing-extent/2-previous[i].wind.north*fixedStep);
        const double density=b.airMass/layerDepth;
        // Exact Coriolis rotation avoids the spurious kinetic-energy growth of Euler rotation.
        const Wind2 rotated{carried.wind.east*rotation+carried.wind.north*deflection,
                            carried.wind.north*rotation-carried.wind.east*deflection};
        // Prescribed geostrophic forcing, regularized near the equator. The
        // projected horizontal layer has no compressive air-mass source/sink.
        const double balancedF=std::copysign(std::max(std::abs(coriolis),1e-4),coriolis==0 ? 1 : coriolis);
        const Wind2 target{clamp(prevailing.east-(pressure[n]-pressure[s])/(2*spacing*density*balancedF),-20.0,20.0),
                           clamp(prevailing.north+(pressure[e]-pressure[w])/(2*spacing*density*balancedF),-20.0,20.0)};
        c.wind.east=clamp(rotated.east+fixedStep*(target.east-rotated.east)/900,-30.0,30.0);
        c.wind.north=clamp(rotated.north+fixedStep*(target.north-rotated.north)/900,-30.0,30.0);
        c.rainRate=0;
    }
    std::vector<Wind2> faces(cells_.size());
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const auto i=index(x,z);
        faces[i]={(cells_[i].wind.east+cells_[index(x+1,z)].wind.east)/2,
                  (cells_[i].wind.north+cells_[index(x,z+1)].wind.north)/2};
    }
    projectWind(faces,resolution,spacing);
    double maxOutgoing=0;
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const auto i=index(x,z);
        maxOutgoing=std::max(maxOutgoing,std::max(0.0,faces[i].east)+std::max(0.0,-faces[index(x-1,z)].east)
                            +std::max(0.0,faces[i].north)+std::max(0.0,-faces[index(x,z-1)].north));
    }
    const double stability=std::min(1.0,0.9*spacing/(fixedStep*std::max(1.0,maxOutgoing)));
    for(auto& f:faces) { f.east*=stability; f.north*=stability; }
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const auto i=index(x,z),e=index(x+1,z),w=index(x-1,z),n=index(x,z+1),s=index(x,z-1);
        auto& c=cells_[i]; const auto& b=background_[i];
        c.wind={(faces[i].east+faces[w].east)/2,(faces[i].north+faces[s].north)/2};
        const auto carried=interpolateCells(previous,(x+0.5)*spacing-extent/2-c.wind.east*fixedStep,
                                                      (z+0.5)*spacing-extent/2-c.wind.north*fixedStep);
        c.uplift=clamp((c.wind.east*(background_[e].terrain-background_[w].terrain)
                     +c.wind.north*(background_[n].terrain-background_[s].terrain))/(2*spacing)
                     *std::exp(-std::max(0.0,layerAltitude_-b.terrain)/3000),-1.0,1.0);
        c.temperature=clamp(carried.temperature+fixedStep*((b.temperature+season-carried.temperature)/1800-0.0098*c.uplift),200.0,320.0);
    }
    if(cloudsEnabled_) {
        std::vector<double> vapour,cloud;
        for(const auto& c:cells_) { vapour.push_back(c.vapour); cloud.push_back(c.cloud); }
        vapour=transportWater(vapour,faces,resolution,spacing,fixedStep,true);
        cloud=transportWater(cloud,faces,resolution,spacing,fixedStep,true);
        for(std::size_t i=0;i<cells_.size();++i) {
            auto& c=cells_[i]; const auto& b=background_[i];
            const double saturation=b.airMass*saturationMixingRatio(c.temperature,b.pressure);
            const double input=3e-5*b.wetness*(0.5+std::hypot(c.wind.east,c.wind.north)/10)
                              *std::max(0.0,1-vapour[i]/saturation)*fixedStep;
            c.vapour=vapour[i]+input; c.cloud=cloud[i];
            evaporation_+=input*spacing*spacing;
            equilibrateMoisture(c.vapour,c.cloud,c.temperature,b.airMass,b.pressure);
            const double rain=std::max(0.0,c.cloud-0.30)*(1-std::exp(-fixedStep/900));
            c.cloud-=rain; c.rainRate=rain/fixedStep; precipitation_+=rain*spacing*spacing;
        }
        for(auto& p:clouds_) {
            const auto c=interpolate(p.x,p.z);
            p.x=wrapCoordinate(p.x+c.wind.east*fixedStep); p.z=wrapCoordinate(p.z+c.wind.north*fixedStep);
        }
    }
    const Vec3 unit=normalized(observer);
    const double project=config_.radius/std::max(0.1,dot(unit,anchor_));
    const double centerX=dot(unit,east_)*project,centerZ=dot(unit,north_)*project;
    for(auto& p:tracers_) {
        const auto c=interpolate(p.x,p.z);
        const double factor=clamp(std::log((p.height+0.1)/0.1)/std::log(1800/0.1),0.0,1.0);
        p.x+=c.wind.east*fixedStep*factor; p.z+=c.wind.north*fixedStep*factor;
        p.height=clamp(p.height+c.uplift*fixedStep*factor,4.0,120.0);
        if(std::hypot(p.x-centerX,p.z-centerZ)>2000) {
            const double angle=tau*randomUnit(p.id+steps_);
            p.x=centerX+1800*std::cos(angle); p.z=centerZ+1800*std::sin(angle);
            p.height=8+100*randomUnit(p.id+steps_+1);
        }
    }
}
} // namespace planet

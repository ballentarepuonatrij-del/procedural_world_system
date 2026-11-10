#include "planet/hydrology.hpp"
#include "planet/rivers.hpp"
#include <numeric>
#include <stdexcept>

namespace planet {
double inertialDischarge(double old,double bedA,double bedB,double waterA,double waterB,
                         double roughness,double spacing,double dt,double gravity,double stress) {
    const double headA=bedA+waterA,headB=bedB+waterB;
    const double depth=std::max(headA,headB)-std::max(bedA,bedB);
    if(depth<1e-7) return 0;
    const double q=(old-gravity*depth*dt*(headB-headA)/spacing+dt*stress/1000)/
                   (1+gravity*dt*roughness*roughness*std::abs(old)/std::pow(depth,7.0/3));
    // The local-inertial approximation neglects advective momentum. Bound it to
    // subcritical flow; waterfalls and hydraulic jumps require a full SWE solver.
    const double limit=depth*std::sqrt(gravity*depth);
    return clamp(q,-limit,limit);
}
RunoffGrid::RunoffGrid(int size,double spacing,double gravity)
    : size_(size),spacing_(spacing),gravity_(gravity) {
    if(size<2 || size>256 || !std::isfinite(spacing) || spacing<=0 ||
       !std::isfinite(gravity) || gravity<=0) throw std::invalid_argument("Invalid runoff grid");
    cells.resize(static_cast<std::size_t>(size*size));
    eastFlow.resize(static_cast<std::size_t>((size+1)*size)); northFlow.resize(eastFlow.size());
}
double RunoffGrid::waterVolume() const {
    double sum=0; for(const auto& c:cells) if(!c.sea && !c.reservoir && !c.masked) sum+=c.water+c.soilWater;
    return sum*spacing_*spacing_;
}
double RunoffGrid::solidMass() const {
    double sum=0; for(const auto& c:cells) if(!c.sea && !c.reservoir && !c.masked) sum+=soilDensity*c.soil+c.sediment;
    return sum*spacing_*spacing_;
}
Wind2 RunoffGrid::velocity(int x,int z) const {
    const auto& c=cells[static_cast<std::size_t>(z*size_+x)];
    if(c.sea || c.reservoir || c.masked || c.water<1e-7) return {};
    Wind2 v{(eastFlow[static_cast<std::size_t>(z*(size_+1)+x)]+eastFlow[static_cast<std::size_t>(z*(size_+1)+x+1)])/(2*c.water),
            (northFlow[static_cast<std::size_t>(z*size_+x)]+northFlow[static_cast<std::size_t>((z+1)*size_+x)])/(2*c.water)};
    const double speed=std::hypot(v.east,v.north),limit=std::sqrt(gravity_*c.water);
    if(speed>limit) { v.east*=limit/speed; v.north*=limit/speed; }
    return v;
}
RunoffExchange RunoffGrid::advance(double seconds,std::span<const double> rain,double evaporation,
                                  bool erosion,bool openBoundary,bool ocean,double seaLevel,std::span<const Wind2> stress) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>120 || rain.size()!=cells.size() ||
       !std::isfinite(evaporation) || evaporation<0 || !std::isfinite(seaLevel) || (!stress.empty() && stress.size()!=cells.size()))
        throw std::invalid_argument("Invalid runoff forcing or timestep");
    for(std::size_t i=0;i<cells.size();++i) {
        const auto& c=cells[i];
        if(!std::isfinite(rain[i]) || rain[i]<0 || rain[i]>0.01 || !std::isfinite(c.rock) ||
           !std::isfinite(c.soil) || c.soil<0 || !std::isfinite(c.water) || c.water<0 ||
           !std::isfinite(c.soilWater) || c.soilWater<0 || !std::isfinite(c.sediment) || c.sediment<0 ||
           !std::isfinite(c.roughness) || c.roughness<=0 || !std::isfinite(c.conductivity) || c.conductivity<0 ||
           !std::isfinite(c.vegetation) || c.vegetation<0 || c.vegetation>1)
            throw std::invalid_argument("Invalid runoff cell");
    }
    for(double q:eastFlow) if(!std::isfinite(q)) throw std::invalid_argument("Nonfinite discharge");
    for(double q:northFlow) if(!std::isfinite(q)) throw std::invalid_argument("Nonfinite discharge");
    for(const auto t:stress) if(!std::isfinite(t.east) || !std::isfinite(t.north)) throw std::invalid_argument("Nonfinite wind stress");
    RunoffExchange result;
    const double area=spacing_*spacing_;
    std::vector<double> outgoing(cells.size()),factor(cells.size()),waterDelta(cells.size()),solidDelta(cells.size());
    const auto active=[&](int i) { return i>=0 && !cells[static_cast<std::size_t>(i)].sea && !cells[static_cast<std::size_t>(i)].reservoir && !cells[static_cast<std::size_t>(i)].masked; };
    const auto faces=[&](auto&& action) {
        const auto forcing=[&](int a,int b,bool east) {
            if(stress.empty()) return 0.0;
            const auto u=stress[static_cast<std::size_t>(a>=0 ? a : b)],v=stress[static_cast<std::size_t>(b>=0 ? b : a)];
            return east ? (u.east+v.east)/2 : (u.north+v.north)/2;
        };
        for(int z=0;z<size_;++z) for(int x=0;x<=size_;++x) {
            const int a=x>0 ? z*size_+x-1 : -1,b=x<size_ ? z*size_+x : -1;
            action(a,b,eastFlow[static_cast<std::size_t>(z*(size_+1)+x)],forcing(a,b,true));
        }
        for(int z=0;z<=size_;++z) for(int x=0;x<size_;++x) {
            const int a=z>0 ? (z-1)*size_+x : -1,b=z<size_ ? z*size_+x : -1;
            action(a,b,northFlow[static_cast<std::size_t>(z*size_+x)],forcing(a,b,false));
        }
    };
    for(double remaining=seconds;remaining>1e-10;) {
        double deepest=0;
        for(std::size_t i=0;i<cells.size();++i) if(!cells[i].sea && !cells[i].reservoir && !cells[i].masked)
            deepest=std::max(deepest,cells[i].water+rain[i]*std::min(remaining,0.5));
        const double dt=std::min({remaining,0.5,0.35*spacing_/std::sqrt(gravity_*std::max(0.001,deepest))});
        for(std::size_t i=0;i<cells.size();++i) {
            auto& c=cells[i]; if(c.sea || c.reservoir || c.masked) continue;
            const double input=rain[i]*dt; c.water+=input; result.rainM3+=input*area;
            const double capacity=porosity*c.soil;
            const double infiltrated=std::min({c.water,std::max(0.0,capacity-c.soilWater),c.conductivity*dt});
            c.water-=infiltrated; c.soilWater+=infiltrated;
            const double saturation=capacity>0 ? clamp(c.soilWater/capacity,0.0,1.0) : 0;
            const double drained=std::min(c.soilWater,0.2*c.conductivity*std::pow(saturation,3)*dt);
            c.soilWater-=drained; result.drainedM3+=drained*area;
            const double surfaceEvap=std::min(c.water,evaporation*dt); c.water-=surfaceEvap;
            const double soilEvap=std::min(c.soilWater,(evaporation*dt-surfaceEvap)*saturation);
            c.soilWater-=soilEvap; result.evaporatedM3+=(surfaceEvap+soilEvap)*area;
        }
        faces([&](int a,int b,double& q,double forcing) {
            if((a>=0 && cells[static_cast<std::size_t>(a)].masked) || (b>=0 && cells[static_cast<std::size_t>(b)].masked)) { q=0; return; }
            if((a<0 || b<0) && !openBoundary) { q=0; return; }
            if(!active(a) && !active(b)) { q=0; return; }
            if(!ocean && ((a>=0 && cells[static_cast<std::size_t>(a)].sea) || (b>=0 && cells[static_cast<std::size_t>(b)].sea))) { q=0; return; }
            const auto& ca=cells[static_cast<std::size_t>(a>=0 ? a : b)];
            const auto& cb=cells[static_cast<std::size_t>(b>=0 ? b : a)];
            const double ha=a<0 ? 0 : (ca.sea || ca.reservoir ? std::max(0.0,(ca.reservoir ? ca.reservoirLevel : seaLevel)-ca.bed()) : ca.water);
            const double hb=b<0 ? 0 : (cb.sea || cb.reservoir ? std::max(0.0,(cb.reservoir ? cb.reservoirLevel : seaLevel)-cb.bed()) : cb.water);
            q=inertialDischarge(q,ca.bed(),cb.bed(),ha,hb,(ca.roughness+cb.roughness)/2,spacing_,dt,gravity_,forcing);
            // Dry open edges permit outflow only. The coast may exchange both ways.
            if(a<0) q=std::min(0.0,q);
            if(b<0) q=std::max(0.0,q);
        });
        std::fill(outgoing.begin(),outgoing.end(),0);
        faces([&](int a,int b,double q,double) {
            const int donor=q>=0 ? a : b;
            if(active(donor)) outgoing[static_cast<std::size_t>(donor)]+=std::abs(q)*dt/spacing_;
        });
        for(std::size_t i=0;i<cells.size();++i) factor[i]=outgoing[i]>0 ? std::min(1.0,cells[i].water/outgoing[i]) : 1;
        std::fill(waterDelta.begin(),waterDelta.end(),0); std::fill(solidDelta.begin(),solidDelta.end(),0);
        faces([&](int a,int b,double& q,double) {
            const int donor=q>=0 ? a : b;
            if(active(donor)) q*=factor[static_cast<std::size_t>(donor)];
            const double amount=q*dt/spacing_;
            const double concentration=active(donor) && cells[static_cast<std::size_t>(donor)].water>1e-12 ?
                cells[static_cast<std::size_t>(donor)].sediment/cells[static_cast<std::size_t>(donor)].water : 0;
            const double solid=amount*concentration;
            if(active(a)) { waterDelta[static_cast<std::size_t>(a)]-=amount; solidDelta[static_cast<std::size_t>(a)]-=solid; }
            if(active(b)) { waterDelta[static_cast<std::size_t>(b)]+=amount; solidDelta[static_cast<std::size_t>(b)]+=solid; }
            const double sign=static_cast<double>(active(a))-static_cast<double>(active(b));
            result.boundaryM3+=sign*amount*area; result.sedimentOutKg+=sign*solid*area;
        });
        for(std::size_t i=0;i<cells.size();++i) if(!cells[i].sea && !cells[i].reservoir && !cells[i].masked) {
            auto& c=cells[i];
            c.water=std::max(0.0,c.water+waterDelta[i]); c.sediment=std::max(0.0,c.sediment+solidDelta[i]);
            if(erosion) {
                const auto v=velocity(static_cast<int>(i)%size_,static_cast<int>(i)/size_);
                const double shear=c.water>1e-7 ? 1000*gravity_*c.roughness*c.roughness*(v.east*v.east+v.north*v.north)/std::cbrt(c.water) : 0;
                const double excess=std::max(0.0,shear-(0.8+3*c.vegetation));
                const double capacity=std::min(80.0,8*excess); // empirical fine-sediment carrying concentration, kg/m3
                const double concentration=c.water>1e-7 ? c.sediment/c.water : 0;
                const double detached=std::min(c.soil*soilDensity,2e-4*(1-0.9*c.vegetation)*excess*
                    (capacity>0 ? std::max(0.0,1-concentration/capacity) : 0)*dt);
                const double deposited=c.water<1e-7 ? c.sediment :
                    std::min(c.sediment,0.001*std::max(0.0,concentration-capacity)*dt);
                c.soil+=(deposited-detached)/soilDensity; c.soil=std::max(0.0,c.soil);
                c.sediment+=detached-deposited;
                result.erodedKg+=detached*area; result.depositedKg+=deposited*area;
                // Erosion reduces pore volume; return displaced pore water to the surface.
                const double released=std::max(0.0,c.soilWater-porosity*c.soil);
                c.soilWater-=released; c.water+=released;
            }
        }
        remaining-=dt;
    }
    return result;
}
Hydrology::Hydrology(const World& world,Vec3 observer,double solarSeconds) : snow(world,observer,solarSeconds),config_(world.config()),anchor_(normalized(observer)),
    east_(east(anchor_)),north_(north(anchor_)),grid_(resolution,spacing,config_.gravity) {
    geometryMask_=(world.modules().enabled(Module::Landforms) ? 1U : 0U)|(world.modules().enabled(Module::Caves) ? 2U : 0U)|(world.modules().enabled(Module::Rivers) ? 4U : 0U)|(world.modules().enabled(Module::InlandWaters) ? 8U : 0U);
    const auto climate=world.climatology(anchor_);
    climateWetness_=clamp(climate.precipitation/1800,0.0,1.0);
    vegetation_=forestBiome(climate.biome) ? 0.8 :
                (climate.biome==Biome::Grassland ? 0.5 : 0.1);
    World relief=world; relief.modules().set(Module::Terrain,true);
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x)
        grid_.cells[static_cast<std::size_t>(z*resolution+x)]=makeCell(relief,originX_+x,originZ_+z);
    totals_.initialWaterM3=grid_.waterVolume(); totals_.initialSolidKg=grid_.solidMass();
    for(std::uint64_t i=0;i<320;++i) tracers_.push_back({0,0,0,1,hash64(config_.seed^i^0x57eaULL)});
    synchronize(world,observer);
}
GroundCell Hydrology::makeCell(const World& world,int x,int z) const {
    const Vec3 p=directionAt(x*spacing,z*spacing);
    GroundCell c;
    const auto terrain=world.relief(p);const double height=terrain.elevation;
    const double soil=0.5+0.9*(0.5+0.5*Noise(config_.seed^0x5011ULL).value(p*(config_.radius/170)));
    c.rock=height-soil; c.soil=c.initialSoil=soil; c.sea=height<config_.seaLevel;
    if(world.modules().enabled(Module::Rivers)) {const auto r=world.waterNetwork()->sample(p);c.reservoir=r.wet && r.level>config_.seaLevel+.02;c.reservoirLevel=r.level;if(c.reservoir) c.sea=false;}
    if(terrain.inland && terrain.waterLevel>height) {c.reservoir=true;c.reservoirLevel=terrain.waterLevel;c.sea=false;}
    c.vegetation=std::max(vegetation_,terrain.wetness); c.roughness=0.025+0.04*vegetation_+.08*terrain.wetness;
    c.conductivity=(1e-6+3e-6*vegetation_)*(1-.8*terrain.wetness);
    c.soilWater=RunoffGrid::porosity*soil*std::max(0.2+0.55*climateWetness_,terrain.wetness*.98);
    if(const auto site=world.caveSite(p)) c.masked=site->protectedGround(p);
    return c;
}
Vec3 Hydrology::directionAt(double x,double z) const { return normalized(anchor_+(east_*x+north_*z)/config_.radius); }
Vec3 Hydrology::position(double x,double z,double altitude) const { return directionAt(x,z)*(config_.radius+altitude); }
void Hydrology::move(const World& world,Vec3 observer,int x,int z,bool resetChart) {
    const double oldWater=grid_.waterVolume(),oldSolid=grid_.solidMass();
    if(resetChart) {
        cache_.clear(); cacheOrder_.clear(); anchor_=normalized(observer); east_=east(anchor_); north_=north(anchor_);
        const auto climate=world.climatology(anchor_);
        climateWetness_=clamp(climate.precipitation/1800,0.0,1.0);
        vegetation_=forestBiome(climate.biome) ? 0.8 :
                    (climate.biome==Biome::Grassland ? 0.5 : 0.1);
    } else {
        for(int j=0;j<resolution;++j) for(int i=0;i<resolution;++i) {
            const Key key{originX_+i,originZ_+j};
            if(!cache_.contains(key)) cacheOrder_.push_back(key);
            cache_[key]=grid_.cells[static_cast<std::size_t>(j*resolution+i)];
        }
    }
    originX_=x; originZ_=z;
    for(int j=0;j<resolution;++j) for(int i=0;i<resolution;++i) {
        const Key key{x+i,z+j}; const auto found=cache_.find(key);
        grid_.cells[static_cast<std::size_t>(j*resolution+i)]=found==cache_.end() ? makeCell(world,key.first,key.second) : found->second;
    }
    while(cache_.size()>65536) { cache_.erase(cacheOrder_.front()); cacheOrder_.pop_front(); }
    std::fill(grid_.eastFlow.begin(),grid_.eastFlow.end(),0); std::fill(grid_.northFlow.begin(),grid_.northFlow.end(),0);
    totals_.domainWaterM3+=grid_.waterVolume()-oldWater; totals_.domainSolidKg+=grid_.solidMass()-oldSolid;
    ++revision_;
}
void Hydrology::synchronize(const World& world,Vec3 observer) {
    snow.synchronize(world,observer);
    if(world.config().seed!=config_.seed || world.config().radius!=config_.radius)
        throw std::invalid_argument("Reset hydrology after changing the planet");
    const bool next=world.modules().enabled(Module::Hydrology);
    if(next!=enabled_) ++revision_;
    enabled_=next; erosion_=world.modules().enabled(Module::Erosion);
    if(!enabled_) return;
    const unsigned geometry=(world.modules().enabled(Module::Landforms) ? 1U : 0U)|(world.modules().enabled(Module::Caves) ? 2U : 0U)|(world.modules().enabled(Module::Rivers) ? 4U : 0U)|(world.modules().enabled(Module::InlandWaters) ? 8U : 0U);
    if(geometry!=geometryMask_) {
        const double beforeWater=grid_.waterVolume(),beforeSolid=grid_.solidMass();
        for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
            auto& c=grid_.cells[static_cast<std::size_t>(z*resolution+x)]; const auto p=directionAt(cellX(x),cellZ(z));
            const auto terrain=world.relief(p);const double h=terrain.elevation; c.rock=h-c.initialSoil; c.sea=h<config_.seaLevel;
            const auto river=world.modules().enabled(Module::Rivers) ? world.waterNetwork()->sample(p) : RiverSample{};
            c.reservoir=river.wet && river.level>config_.seaLevel+.02;c.reservoirLevel=river.level;if(c.reservoir) c.sea=false;
            if(terrain.inland && terrain.waterLevel>h) {c.reservoir=true;c.reservoirLevel=terrain.waterLevel;c.sea=false;}
            const auto site=world.caveSite(p); c.masked=site && site->protectedGround(p);
        }
        totals_.domainWaterM3+=grid_.waterVolume()-beforeWater; totals_.domainSolidKg+=grid_.solidMass()-beforeSolid;
        cache_.clear(); cacheOrder_.clear();
        std::fill(grid_.eastFlow.begin(),grid_.eastFlow.end(),0); std::fill(grid_.northFlow.begin(),grid_.northFlow.end(),0);
        geometryMask_=geometry; ++revision_;
    }
    const Vec3 p=normalized(observer); const double project=dot(p,anchor_);
    if(project<0.99 || length(p-anchor_)*config_.radius>16000) {
        move(world,p,-resolution/2,-resolution/2,true); return;
    }
    const double x=dot(p,east_)*config_.radius/project,z=dot(p,north_)*config_.radius/project;
    if(std::abs(x-(originX_+resolution/2)*spacing)>extent/4 || std::abs(z-(originZ_+resolution/2)*spacing)>extent/4) {
        const int ix=static_cast<int>(std::round(x/(spacing*16)))*16-resolution/2;
        const int iz=static_cast<int>(std::round(z/(spacing*16)))*16-resolution/2;
        move(world,p,ix,iz,false);
    }
}
void Hydrology::step(const World& world,const Weather& weather,double seconds,double experimentRain,const WaterMotion* motion) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>120 || !std::isfinite(experimentRain) || experimentRain<0 || experimentRain>200)
        throw std::invalid_argument("Invalid hydrology step or rain experiment");
    snow.step(world,weather,seconds);
    if(!enabled_ || seconds==0) return;
    std::vector<double> rain(grid_.cells.size());
    std::vector<Wind2> stress(grid_.cells.size());
    double meltVolume=0;
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) {
        const auto p=directionAt(cellX(x),cellZ(z)); const auto air=weather.sample(p);
        const auto index=static_cast<std::size_t>(z*resolution+x);const auto frozen=snow.sample(p);
        const bool coupled=snow.active() && frozen.available;
        const double melt=coupled ? frozen.meltRate : 0;
        const double rainRate=coupled ? (snow.experiment()==SnowExperiment::Natural ? air.rainRate/1000*(1-snow.solidFraction()) : snow.liquidRate()) : air.rainRate/1000;
        rain[index]=rainRate+experimentRain/3.6e6+melt;
        const auto& cell=grid_.cells[index];
        if(!cell.sea && !cell.reservoir && !cell.masked) meltVolume+=melt*seconds*spacing*spacing;
        if(motion && motion->enabled()) {
            const auto wind=motion->windAt(weather,p);
            stress[static_cast<std::size_t>(z*resolution+x)]=windStress({dot(wind,east_),dot(wind,north_)},grid_.velocity(x,z));
        }
    }
    const auto air=weather.sample(directionAt(cellX(resolution/2),cellZ(resolution/2)));
    const double evaporation=weather.cloudsEnabled() ? 4e-8*std::max(0.0,1-air.humidity)*(0.5+length(air.velocity)/10) : 0;
    const auto change=grid_.advance(seconds,rain,evaporation,erosion_,true,world.modules().enabled(Module::Ocean),config_.seaLevel,stress);
    totals_.rainM3+=change.rainM3-meltVolume;totals_.meltM3+=meltVolume; totals_.evaporatedM3+=change.evaporatedM3; totals_.drainedM3+=change.drainedM3;
    totals_.boundaryM3+=change.boundaryM3; totals_.sedimentOutKg+=change.sedimentOutKg;
    totals_.erodedKg+=change.erodedKg; totals_.depositedKg+=change.depositedKg;
    for(auto& p:tracers_) {
        for(double t=seconds;t>1e-9;t-=std::min(t,0.25)) {
            const auto s=sampleLocal(p.x,p.z); const double dt=std::min(t,0.25);
            if(s.available && s.depth>0.001 && p.height<120) {
                p.x+=s.velocity.east*dt; p.z+=s.velocity.north*dt; p.height+=dt;
            } else {
                p.height=0;
                for(std::uint64_t attempt=0;attempt<16;++attempt) {
                    const auto id=hash64(p.id+steps_*17+attempt);
                    const int x=static_cast<int>(id%resolution),z=static_cast<int>((id/resolution)%resolution);
                    p.x=cellX(x); p.z=cellZ(z);
                    if(sampleLocal(p.x,p.z).depth>0.001) break;
                }
                break;
            }
        }
    }
    ++steps_;
}
GroundSample Hydrology::sampleLocal(double x,double z) const {
    if(!enabled_ || !std::isfinite(x) || !std::isfinite(z)) return {};
    const double gx=x/spacing-originX_,gz=z/spacing-originZ_;
    if(gx<0 || gz<0 || gx>=resolution-1 || gz>=resolution-1) return {};
    const int ix=static_cast<int>(std::floor(gx)),iz=static_cast<int>(std::floor(gz));
    const double tx=gx-ix,tz=gz-iz;
    GroundSample s; s.available=true;
    s.weight=smoothstep(0,4,std::min({gx,gz,resolution-1-gx,resolution-1-gz}));
    for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
        const auto& c=grid_.cells[static_cast<std::size_t>((iz+dz)*resolution+ix+dx)];
        if(c.sea || c.reservoir || c.masked) return {};
        const double w=(dx ? tx : 1-tx)*(dz ? tz : 1-tz);
        s.bed+=c.bed()*w; s.depth+=c.water*w;
        s.saturation+=(c.soil>0 ? c.soilWater/(RunoffGrid::porosity*c.soil) : 0)*w;
        s.sedimentKgM3+=(c.water>1e-7 ? c.sediment/c.water : 0)*w;
        s.bedChange+=(c.soil-c.initialSoil)*w;
        const auto v=grid_.velocity(ix+dx,iz+dz); s.velocity.east+=v.east*w; s.velocity.north+=v.north*w;
    }
    return s;
}
GroundSample Hydrology::sample(Vec3 unit) const {
    unit=normalized(unit); const double projection=dot(unit,anchor_);
    if(projection<=0) return {};
    return sampleLocal(dot(unit,east_)*config_.radius/projection,dot(unit,north_)*config_.radius/projection);
}
double Hydrology::ground(const World& world,Vec3 unit,std::optional<double> knownElevation) const {
    const double original=knownElevation ? *knownElevation : world.elevation(unit); const auto s=sample(unit);
    if(world.modules().enabled(Module::Rivers) && world.waterNetwork()->sample(unit).channel) return original+(s.available ? s.bedChange*s.weight : 0);
    return s.available ? mix(original,s.bed,s.weight) : original;
}
RunoffBudget Hydrology::budget() const {
    auto b=totals_; b.storedWaterM3=grid_.waterVolume(); b.storedSolidKg=grid_.solidMass(); return b;
}
Simulation::Simulation(const World& world,Vec3 observer,double seconds,std::optional<double> waterWind,double globalHours) : global(world,seconds,globalHours),weather(world,observer,seconds+global.elapsed(),&global),water(world,observer,seconds+global.elapsed()),motion(world,observer) {
    flora.setCalendarDay((seconds+global.elapsed())/86400);
    motion.setWindExperiment(waterWind); motion.synchronize(world,weather,observer);
}
void Simulation::setClock(double seconds) { weather.setClock(seconds); global.setSolarClock(seconds);water.snow.setClock(seconds); pending_=0; }
void Simulation::setRainExperiment(double rain) {
    if(!std::isfinite(rain) || rain<0 || rain>200) throw std::invalid_argument("Rain experiment must be in [0,200] mm/h");
    experimentRain_=rain;
}
void Simulation::advance(const World& world,Vec3 observer,double seconds) {
    const bool active=world.modules().enabled(Module::Wind) || world.modules().enabled(Module::Hydrology) || world.modules().enabled(Module::WaterMotion) || world.modules().enabled(Module::Snow);
    if(!std::isfinite(seconds) || seconds<0 || seconds>(active ? 120 : 86400))
        throw std::invalid_argument("Simulation timestep exceeds its supported range");
    global.advance(world,0);global.advanceEffects(seconds);
    water.synchronize(world,observer); weather.advance(world,observer,0,&global);
    water.snow.animate(world,weather,observer,seconds);
    flora.advance(world,seconds);
    motion.synchronize(world,weather,observer); motion.advanceClock(seconds);
    if(!active) { weather.setClock(weather.seconds()+pending_+seconds); pending_=0; return; }
    pending_+=seconds;
    while(pending_+1e-9>=Weather::fixedStep) {
        global.advance(world,Weather::fixedStep);
        weather.advance(world,observer,Weather::fixedStep,&global);
        motion.step(world,weather,Weather::fixedStep);
        water.step(world,weather,Weather::fixedStep,experimentRain_,&motion);
        pending_=std::max(0.0,pending_-Weather::fixedStep);
    }
}
} // namespace planet

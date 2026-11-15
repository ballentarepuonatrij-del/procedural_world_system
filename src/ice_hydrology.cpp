#include "planet/hydrology.hpp"
#include "planet/rivers.hpp"
#include <stdexcept>

namespace planet {
IceForcing Hydrology::iceForcing(const World& world,const Weather& weather,int x,int z,const Orbit& sun) const {
    const auto& cell=grid_.cells[static_cast<std::size_t>(z*resolution+x)];
    const auto p=directionAt(cellX(x),cellZ(z));const auto air=weather.sample(p);
    const double level=cell.sea ? config_.seaLevel : cell.reservoir ? cell.reservoirLevel : cell.bed()+cell.water+cell.ice.frozen;
    IceForcing f;f.air=clamp(air.temperature+.0065*(weather.layerAltitude()-level),180.,330.);
    f.wind=world.modules().enabled(Module::Wind) ? length(air.velocity)*.7 : 0;
    f.salinity=cell.sea ? 35 : 0;f.flow=cell.flowSpeed;
    f.solar=sun.irradiance*std::max(0.,dot(p,sun.sunDirection))*.65;
    if(iceExperiment_!=IceExperiment::Natural) {
        f.air=iceExperiment_==IceExperiment::Freeze ? 255.15 : 283.15;
        f.solar=iceExperiment_==IceExperiment::Freeze ? 0 : 350;f.wind=3;
    }
    return f;
}
void Hydrology::advanceIceField(const World& world,double seconds,std::span<const IceForcing> forcing) {
    const double area=spacing*spacing;
    for(std::size_t i=0;i<grid_.cells.size();++i) {
        auto& cell=grid_.cells[i];if(cell.masked || (!enabled_ && !cell.sea && !cell.reservoir)) continue;
        if((cell.sea && !world.modules().enabled(Module::Ocean)) ||
           (cell.reservoir && !world.modules().enabled(cell.flowSpeed>0 ? Module::Rivers : Module::InlandWaters))) continue;
        const bool reservoir=cell.sea || cell.reservoir;
        const double total=reservoir ? std::max(0.,(cell.sea ? config_.seaLevel : cell.reservoirLevel)-cell.bed()) : cell.water+cell.ice.frozen;
        if(cell.ice.frozen>total) {iceTotals_.domainM3-=(cell.ice.frozen-total)*area;cell.ice.frozen=total;}
        double liquid=std::max(0.,total-cell.ice.frozen);
        const auto change=advanceIce(cell.ice,liquid,seconds,forcing[i]);
        if(!reservoir) cell.water=liquid;
        iceTotals_.frozenM3+=change.frozen*area;iceTotals_.meltedM3+=change.melted*area;iceTotals_.heatJ+=change.heat*area;
    }
}
IceSample Hydrology::iceAt(Vec3 p,double depth,double level,bool ocean,double flow) const {
    if(!iceEnabled_ || depth<=0) return {};
    const double salinity=ocean ? 35 : 0,tf=freezingPoint(salinity);
    const double air=iceReferenceAir_-.0065*(level-iceReferenceLevel_);
    const double background=iceExperiment_==IceExperiment::Natural ? referenceIce(air,depth,salinity,flow) : 0;
    double frozen=background,temperature=std::max(tf,air);
    p=normalized(p);const double projection=dot(p,anchor_);
    if(projection>0) {
        const double gx=dot(p,east_)*config_.radius/projection/spacing-originX_;
        const double gz=dot(p,north_)*config_.radius/projection/spacing-originZ_;
        if(gx>=0 && gz>=0 && gx<resolution-1 && gz<resolution-1) {
            const int x=static_cast<int>(gx),z=static_cast<int>(gz);const double tx=gx-x,tz=gz-z;
            double local=0,localTemperature=0;
            for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
                const auto& c=grid_.cells[static_cast<std::size_t>((z+j)*resolution+x+i)];
                const double weight=(i ? tx : 1-tx)*(j ? tz : 1-tz);
                local+=(c.masked ? 0 : c.ice.frozen)*weight;localTemperature+=c.ice.temperature*weight;
            }
            const double weight=smoothstep(0,4,std::min({gx,gz,resolution-1-gx,resolution-1-gz}));
            frozen=mix(background,local,weight);temperature=mix(temperature,localTemperature,weight);
        }
    }
    return iceProperties(clamp(frozen,0.,depth),salinity,temperature);
}
IceBudget Hydrology::iceBudget() const {auto b=iceTotals_;b.storedM3=grid_.frozenVolume();return b;}
void Hydrology::restoreLegacyIce(const World& world) {
    iceTotals_={};iceEnabled_=world.modules().enabled(Module::Ice);iceExperiment_=IceExperiment::Natural;
    const auto network=world.modules().enabled(Module::Rivers) ? world.waterNetwork() : nullptr;
    const auto restore=[&](GroundCell& c,int x,int z) {
        const double level=c.sea ? config_.seaLevel : c.reservoir ? c.reservoirLevel : c.bed();
        c.ice={0,std::max(freezingPoint(c.sea ? 35 : 0),iceReferenceAir_-.0065*(level-iceReferenceLevel_))};c.flowSpeed=0;
        if(network && c.reservoir) {const auto r=network->sample(directionAt(x*spacing,z*spacing));if(r.wet && std::abs(r.level-c.reservoirLevel)<.01)c.flowSpeed=r.speed;}
    };
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) restore(grid_.cells[static_cast<std::size_t>(z*resolution+x)],originX_+x,originZ_+z);
    for(auto& [key,c]:cache_) restore(c,key.first,key.second);
}
void Hydrology::previewIce(const World& world,const Weather& weather,double seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>7*86400) throw std::invalid_argument("Ice preview must be 0..168 hours");
    if(!iceEnabled_ || seconds==0) return;
    std::vector<IceForcing> forcing(grid_.cells.size());
    const auto sun=orbit(config_,weather.seconds());
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) forcing[static_cast<std::size_t>(z*resolution+x)]=iceForcing(world,weather,x,z,sun);
    for(double left=seconds;left>1e-9;) {const double dt=std::min(120.,left);advanceIceField(world,dt,forcing);left-=dt;}
    ++steps_;++revision_;
}
} // namespace planet

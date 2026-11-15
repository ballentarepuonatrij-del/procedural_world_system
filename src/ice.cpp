#include "planet/ice.hpp"
#include <stdexcept>

namespace planet {
IceSample iceProperties(double frozen,double salinity,double temperature) {
    IceSample s;s.temperature=temperature;s.submerged=std::max(0.,frozen);
    s.thickness=s.submerged*liquidDensity(salinity)/iceDensity;
    s.freeboard=s.thickness-s.submerged;s.cover=iceCoverage(frozen,salinity);
    s.transmission=std::exp(-2.2*s.thickness)*(1-.35*smoothstep(.08,.6,s.thickness));
    s.supports=s.thickness>=.10;return s;
}
double referenceIce(double air,double depth,double salinity,double flow) {
    // A climatological initial condition, not a reconstruction of past weather.
    const double thickness=.030*std::sqrt(std::max(0.,freezingPoint(salinity)-air)*30)/(1+4*flow*flow);
    return clamp(thickness*iceDensity/liquidDensity(salinity),0.,std::max(0.,depth));
}
double iceEnthalpy(const IceColumn& c,double liquid,double salinity) {
    const double rho=liquidDensity(salinity),tf=freezingPoint(salinity);
    const double sensible=c.frozen<=1e-12 ? waterHeatCapacity*std::min(liquid,2.)*std::max(0.,c.temperature-tf) :
        liquid<=1e-12 ? iceHeatCapacity*std::min(c.frozen,2.)*std::min(0.,c.temperature-tf) : 0;
    return rho*(sensible-latentHeatFusion*c.frozen);
}
IceExchange advanceIce(IceColumn& c,double& liquid,double seconds,const IceForcing& f) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>120 || !std::isfinite(liquid) || liquid<0 ||
       !std::isfinite(c.frozen) || c.frozen<0 || !std::isfinite(c.temperature) || c.temperature<100 || c.temperature>400 ||
       !std::isfinite(f.air) || f.air<150 || f.air>350 || !std::isfinite(f.solar) || f.solar<0 || f.solar>3000 ||
       !std::isfinite(f.wind) || f.wind<0 || f.wind>200 || !std::isfinite(f.salinity) || f.salinity<0 || f.salinity>45 ||
       !std::isfinite(f.flow) || f.flow<0 || f.flow>50 || !std::isfinite(f.emissivity) || f.emissivity<0 || f.emissivity>1)
        throw std::invalid_argument("Invalid ice column, forcing or timestep");
    IceExchange result;if(seconds==0) return result;
    const double total=liquid+c.frozen,rho=liquidDensity(f.salinity),tf=freezingPoint(f.salinity);
    if(total<1e-12) {c.temperature=std::max(tf,f.air);return result;}
    const double before=iceEnthalpy(c,liquid,f.salinity);
    const auto transfer=[&](double temperature,bool solid) {
        const double thickness=c.frozen*rho/iceDensity,cover=iceCoverage(c.frozen,f.salinity);
        const double airConductance=(10+4*std::sqrt(f.wind))/(1+2*f.flow*f.flow);
        const double radiation=f.emissivity*stefanBoltzmann*(std::pow(f.air-6,4)-std::pow(temperature,4));
        const double conductance=airConductance+4*f.emissivity*stefanBoltzmann*std::pow(temperature,3);
        // Most shortwave penetrates the mixed liquid layer; ice absorbs more.
        const double solar=f.solar*mix(.12,.35,cover);
        const double resistance=1+conductance*thickness*(solid ? .5 : 1)/2.2;
        return std::pair{(airConductance*(f.air-temperature)+radiation+solar)/resistance+20*f.flow*f.flow,
                         conductance/resistance};
    };
    for(double remaining=seconds;remaining>1e-9;) {
        const auto [atFreeze,kFreeze]=transfer(tf,liquid<1e-12);(void)kFreeze;
        const bool warm=c.frozen<1e-12 && (c.temperature>tf+1e-9 || atFreeze>=0);
        const bool cold=liquid<1e-12 && (c.temperature<tf-1e-9 || atFreeze<=0);
        if(warm || cold) {
            const double capacity=rho*(warm ? waterHeatCapacity : iceHeatCapacity)*std::min(total,2.);
            const auto [heat,k]=transfer(c.temperature,cold);
            const double equilibrium=c.temperature+heat/k;
            const double next=equilibrium+(c.temperature-equilibrium)*std::exp(-k*remaining/capacity);
            if((warm && next>=tf) || (cold && next<=tf)) {c.temperature=clamp(next,100.,400.);break;}
            const double crossing=-capacity/k*std::log((tf-equilibrium)/(c.temperature-equilibrium));
            remaining-=clamp(crossing,0.,remaining);c.temperature=tf;
            continue;
        }
        c.temperature=tf;
        if(std::abs(atFreeze)<1e-12) break;
        const double available=atFreeze<0 ? liquid : c.frozen;
        const double dt=std::min({remaining,20.,available*rho*latentHeatFusion/std::abs(atFreeze)});
        const double amount=std::min(available,std::abs(atFreeze)*dt/(rho*latentHeatFusion));
        if(atFreeze<0) {c.frozen+=amount;result.frozen+=amount;}
        else {c.frozen-=amount;result.melted+=amount;}
        c.frozen=clamp(c.frozen,0.,total);liquid=total-c.frozen;
        remaining-=dt;
    }
    result.heat=iceEnthalpy(c,liquid,f.salinity)-before;return result;
}
std::string_view iceExperimentName(IceExperiment v) {
    constexpr std::array names{"natural","freeze","thaw"};return names.at(static_cast<std::size_t>(v));
}
std::optional<IceExperiment> iceExperiment(std::string_view value) {
    for(unsigned i=0;i<3;++i) if(iceExperimentName(static_cast<IceExperiment>(i))==value) return static_cast<IceExperiment>(i);
    return {};
}
} // namespace planet

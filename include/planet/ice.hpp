#pragma once
#include "planet/model.hpp"

namespace planet {
constexpr double iceDensity=917,waterHeatCapacity=4186,iceHeatCapacity=2100,latentHeatFusion=333550;
constexpr double freezingPoint(double salinity) {return 273.15-.054*salinity;} // K; salinity in g/kg
constexpr double liquidDensity(double salinity) {return 1000+.714*salinity;}
inline double iceCoverage(double frozen,double salinity=0) {return frozen>0 ? smoothstep(.0005,.015,frozen*liquidDensity(salinity)/iceDensity) : 0;}
struct IceColumn {
    double frozen=0; // m of liquid-water equivalent, at the local water density
    double temperature=273.15; // mixed liquid, or fully frozen column, in K
};
struct IceForcing {
    double air=268.15,solar=0,wind=0,salinity=0,flow=0,emissivity=.96;
};
struct IceExchange {double frozen=0,melted=0,heat=0;}; // m water equivalent; J/m2 into the column
// A local 2 m mixed layer, latent heat and a conducting ice lid. The liquid and
// frozen reservoirs exchange mass exactly; neither depth is a visual opacity.
IceExchange advanceIce(IceColumn& column,double& liquid,double seconds,const IceForcing& forcing);
double iceEnthalpy(const IceColumn& column,double liquid,double salinity);
double referenceIce(double air,double depth,double salinity,double flow=0); // initial seasonal estimate, m water equivalent
struct IceSample {
    double thickness=0,cover=0,freeboard=0,submerged=0,transmission=1,temperature=273.15;
    bool supports=false;
    double waveFactor() const {return 1-cover;}
};
IceSample iceProperties(double frozen,double salinity,double temperature=273.15);
struct IceBudget {
    double initialM3=0,storedM3=0,frozenM3=0,meltedM3=0,domainM3=0,heatJ=0;
    double residual() const {return storedM3+meltedM3-initialM3-frozenM3-domainM3;}
};
enum class IceExperiment { Natural, Freeze, Thaw };
std::string_view iceExperimentName(IceExperiment value);
std::optional<IceExperiment> iceExperiment(std::string_view value);
} // namespace planet

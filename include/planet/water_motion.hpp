#pragma once
#include "planet/weather.hpp"

namespace planet {
class Hydrology;
// Stress in N/m2, wind at a prescribed 10 m reference height, velocity in m/s.
Wind2 windStress(Wind2 wind,Wind2 current={});
double waveFrequency(double waveNumber,double depth,double gravity=standardGravity);
Wind2 advanceSlab(Wind2 current,Wind2 stress,double mixedDepth,double coriolis,double drag,double seconds);
double waterPressure(double airPressure,double depth,bool ocean,double gravity=standardGravity);
struct WaveSurface { double height{}; Vec3 gradient{}; };
struct LiquidSample {
    bool present=false,ocean=false,river=false;
    double bed{},level{},depth{},sediment{};
    Vec3 current{};
};
class WaterMotion {
public:
    WaterMotion(const World& world,Vec3 observer);
    void synchronize(const World& world,const Weather& weather,Vec3 observer);
    void step(const World& world,const Weather& weather,double seconds);
    void advanceClock(double seconds);
    void setWindExperiment(std::optional<double> speed);
    std::optional<double> windExperiment() const { return experiment_; }
    Vec3 windAt(const Weather& weather,Vec3 unit) const;
    WaveSurface waves(Vec3 unit,double depth,bool ocean=true,double minimumWavelength=0) const;
    Vec3 current(Vec3 unit,double belowSurface=0) const;
    Vec3 forcingWind() const { return wind_; }
    bool enabled() const { return enabled_; }
    double significantHeight() const;
    double seconds() const { return time_; }
    double mixedDepth() const { return mixedDepth_; }
    const std::vector<Vec3>& probes() const { return probes_; }
private:
    struct Component { Vec3 direction{}; double k{},omega{},phase{},variance{}; };
    std::array<Component,24> waves_{};
    std::array<double,24> targets(Vec3 wind) const;
    Config config_;
    Vec3 anchor_,east_,north_,wind_{};
    Wind2 current_{};
    std::vector<Vec3> probes_;
    std::optional<double> experiment_;
    double time_=0,mixedDepth_=15,fetch_=5000;
    bool enabled_=false,initialized_=false,oceanHere_=false;
    std::shared_ptr<const RiverNetwork> circulation_;
    bool globalFlow_=false;
};
LiquidSample liquidAt(const World& world,const Hydrology* water,const WaterMotion* motion,Vec3 unit);
} // namespace planet

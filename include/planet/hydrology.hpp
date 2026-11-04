#pragma once
#include "planet/weather.hpp"
#include "planet/water_motion.hpp"
#include "planet/flora.hpp"
#include "planet/global_weather.hpp"
#include <map>
#include <deque>

namespace planet {
struct GroundCell {
    double rock{}, soil=1, initialSoil=1; // m; immobile bedrock and erodible soil
    double water{}, soilWater{};         // m of water per unit ground area
    double sediment{};                  // kg/m2 suspended solids
    double roughness=0.04, conductivity=2e-6, vegetation=0;
    bool sea=false;                     // prescribed sea-level reservoir, not simulated ocean
    bool masked=false;                  // unresolved multilevel cave region: impermeable solver boundary
    double bed() const { return rock+soil; }
};
struct RunoffExchange {
    double rainM3{}, evaporatedM3{}, drainedM3{}, boundaryM3{}, sedimentOutKg{};
    double erodedKg{}, depositedKg{};
};
// Local-inertial shallow-water discharge per unit face width (m2/s).
// Hydrostatic reconstruction preserves a lake at rest and dry topographic barriers.
double inertialDischarge(double old,double bedA,double bedB,double waterA,double waterB,
                         double roughness,double spacing,double dt,double gravity=standardGravity,double stress=0);
class RunoffGrid {
public:
    static constexpr double soilDensity=1600, porosity=0.4;
    RunoffGrid(int size,double spacing,double gravity=standardGravity);
    RunoffExchange advance(double seconds,std::span<const double> rainMetresPerSecond,
                           double evaporationRate,bool erosion,bool openBoundary,
                           bool ocean,double seaLevel,std::span<const Wind2> stress={});
    double waterVolume() const;
    double solidMass() const;
    Wind2 velocity(int x,int z) const;
    int size() const { return size_; }
    double spacing() const { return spacing_; }
    std::vector<GroundCell> cells;
    // Face discharges: east n*(n+1), north (n+1)*n. Positive east/north.
    std::vector<double> eastFlow,northFlow;
private:
    int size_;
    double spacing_,gravity_;
};
struct RunoffBudget {
    double initialWaterM3{}, storedWaterM3{}, rainM3{}, evaporatedM3{}, drainedM3{}, boundaryM3{}, domainWaterM3{};
    double initialSolidKg{}, storedSolidKg{}, sedimentOutKg{}, domainSolidKg{}, erodedKg{}, depositedKg{};
    double waterResidual() const { return storedWaterM3+evaporatedM3+drainedM3+boundaryM3-initialWaterM3-rainM3-domainWaterM3; }
    double solidResidual() const { return storedSolidKg+sedimentOutKg-initialSolidKg-domainSolidKg; }
};
struct GroundSample {
    bool available=false;
    double bed{}, depth{}, saturation{}, sedimentKgM3{}, bedChange{}, weight{};
    Wind2 velocity{};
};
class Hydrology {
public:
    static constexpr int resolution=64;
    static constexpr double spacing=4, extent=resolution*spacing, maximumTimeScale=120;
    Hydrology(const World& world,Vec3 observer);
    void synchronize(const World& world,Vec3 observer);
    void step(const World& world,const Weather& weather,double seconds,double experimentRainMmH=0,const WaterMotion* motion=nullptr);
    GroundSample sample(Vec3 unit) const;
    GroundSample sampleLocal(double x,double z) const;
    double ground(const World& world,Vec3 unit) const;
    Vec3 directionAt(double x,double z) const;
    Vec3 position(double x,double z,double altitude) const;
    double cellX(int x) const { return (originX_+x)*spacing; }
    double cellZ(int z) const { return (originZ_+z)*spacing; }
    const RunoffGrid& grid() const { return grid_; }
    const std::vector<WeatherParticle>& tracers() const { return tracers_; }
    Vec3 flowVector(Wind2 v) const { return east_*v.east+north_*v.north; }
    bool enabled() const { return enabled_; }
    bool erosionEnabled() const { return erosion_; }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t steps() const { return steps_; }
    RunoffBudget budget() const;
private:
    GroundCell makeCell(const World& world,int x,int z) const;
    void move(const World& world,Vec3 observer,int x,int z,bool resetChart);
    Config config_;
    Vec3 anchor_,east_,north_;
    int originX_=-resolution/2,originZ_=-resolution/2;
    RunoffGrid grid_{resolution,spacing};
    using Key=std::pair<int,int>;
    std::map<Key,GroundCell> cache_;
    std::deque<Key> cacheOrder_;
    std::vector<WeatherParticle> tracers_;
    RunoffBudget totals_{};
    double climateWetness_=0.5,vegetation_=0.5;
    std::uint64_t revision_=0,steps_=0;
    bool enabled_=false,erosion_=false;
    unsigned geometryMask_=0;
};
// Shared fixed clock: weather rain over each 2 s interval feeds the ground once.
// This avoids frame-rate-dependent rain when the renderer runs at different FPS.
class Simulation {
public:
    Simulation(const World& world,Vec3 observer,double seconds,std::optional<double> waterWind={},double globalHours=0);
    void advance(const World& world,Vec3 observer,double modelSeconds);
    void setClock(double seconds);
    void setRainExperiment(double mmPerHour);
    double rainExperiment() const { return experimentRain_; }
    GlobalWeather global;
    Weather weather;
    Hydrology water;
    WaterMotion motion;
    Flora flora;
private:
    double pending_=0,experimentRain_=0;
};
} // namespace planet

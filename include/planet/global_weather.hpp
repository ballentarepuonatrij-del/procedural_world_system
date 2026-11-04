#pragma once
#include "planet/weather.hpp"

namespace planet {
enum class StormKind { Cyclone, Hurricane, Thunderstorm };
std::string_view stormName(StormKind kind);
std::optional<StormKind> stormKind(std::string_view name);
struct Storm {
    std::uint64_t id=0,generation=0;
    StormKind kind=StormKind::Cyclone;
    Vec3 center{};
    double age=0,lifetime=0,radius=0,deficit=0;
};
// Holland-style pressure profile and gradient-wind balance, in Pa and m/s.
double cycloneWind(double radius,double peakRadius,double deficit,double coriolis,double shape=1.4);
double tropicalSupport(double temperatureK,double latitudeRadians,bool ocean,double humidity);
// Prescribed basin anomaly about the zonal annual mean; not ocean heat transport.
double oceanTemperature(const Config& config,Vec3 direction,double annualMeanK);
double sphericalCellArea(int row,int columns,int rows,double radius);
// Conservative prescribed-wind column-water advection on a closed sphere.
std::vector<double> transportSphere(std::span<const double> water,std::span<const Wind2> wind,
                                   int columns,int rows,double radius,double seconds);
struct GlobalSample {
    bool available=false;
    Vec3 velocity{}; // Estimated 10 m surface wind; m/s in world coordinates.
    double pressure=101325,temperature=288.15,humidity=0,cloud=0,rainRate=0;
    double severity=0,convection=0; // Dimensionless subgrid forcing, not measured CAPE.
    StormKind kind=StormKind::Cyclone;
};
struct GlobalColumn {
    double vapour=0,cloud=0,temperature=280,rainRate=0;
};
struct LightningStrike {
    std::uint64_t id=0,generation=0,cycle=0;
    Vec3 direction{};
    double time=0,strength=0;
};
class GlobalWeather {
public:
    static constexpr int columns=72,rows=36;
    static constexpr double fixedStep=300;
    GlobalWeather(const World& world,double solarSeconds=0,double previewHours=0);
    void advance(const World& world,double seconds);
    void setSolarClock(double seconds);
    void advanceEffects(double seconds);
    GlobalSample sample(Vec3 direction) const;
    Vec3 observation(StormKind kind) const;
    std::optional<Storm> strongest(StormKind kind) const;
    double flash(Vec3 direction) const;
    std::vector<LightningStrike> lightning() const;
    double elapsed() const { return elapsed_+pending_; }
    double effectsTime() const { return effects_; }
    std::uint64_t steps() const { return steps_; }
    bool enabled() const { return enabled_; }
    bool cloudsEnabled() const { return cloudsEnabled_; }
    const std::vector<Storm>& storms() const { return storms_; }
    const std::vector<GlobalColumn>& cells() const { return cells_; }
    WaterBudget budget() const;
    Vec3 cellDirection(int x,int y) const;
private:
    struct Habitat { Vec3 direction; double temperature,pressure,airMass,wetness,area; bool ocean; };
    void background(const World& world);
    std::optional<Storm> seedStorm(const World& world,StormKind kind,std::uint64_t id,std::uint64_t generation) const;
    GlobalSample forcing(Vec3 p) const;
    void step(const World& world);
    double waterMass() const;
    Config config_;
    std::vector<Habitat> habitat_;
    std::vector<GlobalColumn> cells_;
    std::vector<Storm> storms_;
    double elapsed_=0,pending_=0,effects_=0,solarTime_=0,initialWater_=0,evaporation_=0,precipitation_=0;
    std::uint64_t steps_=0;
    unsigned geometry_=0;
    bool enabled_=false,cloudsEnabled_=false;
};
} // namespace planet

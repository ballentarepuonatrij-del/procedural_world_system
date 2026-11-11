#pragma once
#include "planet/model.hpp"
#include <span>
#include <vector>

namespace planet {
class GlobalWeather;
struct Wind2 { double east{}, north{}; };
// Liquid-water saturation, Tetens approximation (Pa), and moist phase adjustment.
double saturationPressure(double temperatureK);
double saturationMixingRatio(double temperatureK,double pressurePa);
// Columns in kg/m2; dry-air column mass in kg/m2. Conserves total water and
// cp*airMass*T + Lv*vapour during condensation/evaporation.
void equilibrateMoisture(double& vapour,double& cloud,double& temperature,
                        double airMass,double pressure);
// Conservative, first-order upwind finite-volume transport on a periodic square.
// Rejects negative water, nonfinite input and an outgoing Courant number > 1.
std::vector<double> transportWater(std::span<const double> water,std::span<const Wind2> wind,
                                   int cells,double spacing,double seconds,bool faceCentered=false);
// Wind2 stores velocity on each cell's east/north face. A periodic discrete
// Poisson solve removes divergence, consistently with transportWater's fluxes.
void projectWind(std::span<Wind2> faces,int cells,double spacing);

struct WeatherCell {
    double vapour{}, cloud{}, temperature{};
    Wind2 wind{};
    double uplift{}, rainRate{}; // m/s; kg/m2/s leaving the cloud layer
};
struct WeatherParticle {
    double x{}, z{}, height{}, size{};
    std::uint64_t id{};
};
struct WeatherSample {
    bool available{};
    Vec3 velocity{};
    Wind2 wind{};
    double temperature{}, humidity{}, cloudWater{}, rainRate{}, pressure{};
};
struct WaterBudget {
    double initialKg{}, storedKg{}, evaporatedKg{}, precipitatedKg{}, domainExchangeKg{};
    double synopticExchangeKg{};
    double residualKg() const { return storedKg+precipitatedKg-initialKg-evaporatedKg-domainExchangeKg-synopticExchangeKg; }
};
class Weather {
    friend class StateCodec;
public:
    static constexpr int resolution=32;
    static constexpr double spacing=750, extent=resolution*spacing, layerDepth=800;
    static constexpr double fixedStep=2, maximumTimeScale=600;
    Weather(const World& world,Vec3 observer,double seconds,const GlobalWeather* global=nullptr);
    // At most 120 model seconds per call when wind is enabled; no dropped steps.
    void advance(const World& world,Vec3 observer,double modelSeconds,const GlobalWeather* global=nullptr);
    void setClock(double seconds);
    WeatherSample sample(Vec3 unit) const;
    WeatherSample sampleLocal(double x,double z) const;
    Vec3 directionAt(double x,double z) const;
    Vec3 position(double x,double z,double altitude) const;
    double groundAt(double x,double z) const;
    double layerAltitude() const { return layerAltitude_; }
    double seconds() const { return time_+pending_; }
    std::uint64_t steps() const { return steps_; }
    bool windEnabled() const { return windEnabled_; }
    bool cloudsEnabled() const { return cloudsEnabled_; }
    WaterBudget budget() const;
    const std::vector<WeatherCell>& cells() const { return cells_; }
    const std::vector<WeatherParticle>& clouds() const { return clouds_; }
    const std::vector<WeatherParticle>& tracers() const { return tracers_; }
private:
    struct Background { double temperature{}, pressure{}, airMass{}, terrain{}, wetness{}, pressureA{}, pressureB{}, forcingWeight{}; };
    void background(const World& world);
    void recenter(const World& world,Vec3 observer,const GlobalWeather* global);
    void seedParticles();
    void step(const World& world,Vec3 observer,const GlobalWeather* global);
    WeatherCell interpolate(double x,double z) const;
    double waterMass() const;
    Config config_;
    Vec3 anchor_{}, east_{}, north_{};
    std::vector<WeatherCell> cells_;
    std::vector<Background> background_;
    std::vector<WeatherParticle> clouds_,tracers_;
    double layerAltitude_=1800, time_=0, pending_=0, annualTemperature_=288.15;
    double initialWater_=0, evaporation_=0, precipitation_=0, domainExchange_=0;
    double synopticExchange_=0;
    std::uint64_t steps_=0;
    unsigned backgroundModules_=~0U;
    bool windEnabled_=false, cloudsEnabled_=false;
};
} // namespace planet

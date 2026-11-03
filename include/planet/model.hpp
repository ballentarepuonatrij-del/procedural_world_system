#pragma once
#include "planet/math.hpp"
#include <array>
#include <string_view>

namespace planet {
constexpr double stefanBoltzmann = 5.670374419e-8;
constexpr double gasConstantAir = 287.05287;
constexpr double standardGravity = 9.80665;

enum class Module : unsigned { Terrain, Ocean, Atmosphere, Climate, Biomes, SurfaceDetail, Wind, Clouds, Count };
constexpr auto moduleCount = static_cast<unsigned>(Module::Count);
constexpr std::array<std::string_view, moduleCount> moduleNames{"Terrain", "Ocean", "Atmosphere", "Climate", "Biomes", "Surface detail", "Wind", "Clouds"};
class Modules {
public:
    bool requested(Module module) const;
    bool enabled(Module module) const;
    void set(Module module, bool enabled);
    void toggle(Module module) { set(module, !requested(module)); }
private:
    std::array<bool, moduleCount> active_=[] { std::array<bool,moduleCount> values{}; values.fill(true); return values; }();
};

struct Config {
    std::uint64_t seed = 42;
    double radius = 6'371'000.0;
    double gravity = standardGravity;
    double solarConstant = 1361.0;
    double obliquity = radians(23.43928);
    double eccentricity = 0.0167086;
    double day = 86400.0;
    double year = 365.256363004 * 86400.0;
    double seaLevel = 0.0;
    double emissivity = 0.61;
    double albedo = 0.30;
};
struct Orbit {
    Vec3 sunDirection;
    double distanceAU;
    double irradiance;
    double declination;
};
Orbit orbit(const Config& config, double seconds);
double dailyInsolation(const Config& config, double latitudeRadians, double seconds);

struct Atmosphere {
    double temperature; // K
    double pressure;    // Pa
    double density;     // kg/m^3
};
// Dry, hydrostatic, layered standard atmosphere. Geometric altitude in metres.
Atmosphere standardAtmosphere(double altitude, double seaTemperature = 288.15,
                              double surfacePressure = 101325.0, double radius = 6'371'000.0,
                              double gravity = standardGravity);

enum class Biome { Ocean, Ice, Tundra, Desert, Grassland, Forest, Rainforest, Rock };
std::string_view biomeName(Biome biome);
struct Color { float r, g, b; };
struct Sample {
    double elevation{};
    double temperature{};
    double pressure{};
    double density{};
    double precipitation{}; // mm/year; empirical climatology, not a water solver
    double insolation{};    // instantaneous top-of-atmosphere W/m^2
    Biome biome = Biome::Rock;
    bool ocean{};
};

class World {
public:
    explicit World(Config config = {});
    const Config& config() const { return config_; }
    Modules& modules() { return modules_; }
    const Modules& modules() const { return modules_; }
    double elevation(Vec3 unit) const;
    double surface(Vec3 unit) const;
    double annualInsolation(double latitudeRadians) const;
    Sample sample(Vec3 unit, double seconds) const;
    Sample climatology(Vec3 unit) const; // Annual energy-balance habitat, independent of view time.
    Color color(const Sample& sample) const;
    Vec3 findLand() const;
private:
    Sample evaluate(Vec3 unit, double seconds, bool annualMean) const;
    struct Plate { Vec3 center; Vec3 velocity; double continental; };
    Config config_;
    Modules modules_;
    Noise noise_;
    std::array<Plate, 24> plates_{};
    std::array<double, 181> annualSolar_{};
};
} // namespace planet

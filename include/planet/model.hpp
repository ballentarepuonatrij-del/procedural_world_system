#pragma once
#include "planet/math.hpp"
#include "planet/landforms.hpp"
#include <array>
#include <string_view>
#include <memory>

namespace planet {
constexpr double stefanBoltzmann = 5.670374419e-8;
constexpr double gasConstantAir = 287.05287;
constexpr double standardGravity = 9.80665;

enum class Module : unsigned { Terrain, Ocean, Atmosphere, Climate, Biomes, SurfaceDetail, Wind, Clouds, Hydrology, Erosion, Landforms, Caves, WaterMotion, PlantGrowth, GlobalWeather, Sound, Rivers, InlandWaters, Fauna, Snow, Phenomena, Count };
constexpr auto moduleCount = static_cast<unsigned>(Module::Count);
constexpr std::array<std::string_view, moduleCount> moduleNames{"Terrain", "Ocean", "Atmosphere", "Climate", "Biomes", "Surface detail", "Wind", "Clouds", "Surface water", "Erosion", "Landforms", "Caves", "Water motion", "Plant growth", "Global weather", "Sound", "Rivers", "Lakes and wetlands", "Fauna", "Snow", "Atmospheric phenomena"};
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
    bool operator==(const Config&) const = default;
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

enum class Biome { Ocean, Ice, Tundra, Desert, Grassland, Forest, Rainforest, Rock, Wetland, Freshwater, Beach, BorealForest, TemperateRainforest, AlpineMeadow };
std::string_view biomeName(Biome biome);
bool forestBiome(Biome biome);
std::optional<Biome> forestType(std::string_view name);
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
    Landform landform=Landform::Lowland;
    bool freshwater=false,inland=false;
    double waterLevel=0,waterDepth=0,wetness=0,sand=0;
};

class RiverNetwork;
class LandscapeAtlas;
class World {
public:
    explicit World(Config config = {});
    const Config& config() const { return config_; }
    Modules& modules() { return modules_; }
    const Modules& modules() const { return modules_; }
    double elevation(Vec3 unit) const;
    double naturalElevation(Vec3 unit) const { return rawRelief(unit).elevation; }
    std::shared_ptr<const RiverNetwork> waterNetwork() const;
    std::shared_ptr<const LandscapeAtlas> landscape() const;
    double surface(Vec3 unit) const;
    Relief relief(Vec3 unit) const;
    std::optional<CaveSite> caveSite(Vec3 unit,double reach=650,bool requireEnabled=true) const;
    Landmark findLandmark(Landform type,Vec3 around) const;
    Landmark findCoast() const;
    double annualInsolation(double latitudeRadians) const;
    Sample sample(Vec3 unit, double seconds) const;
    Sample climatology(Vec3 unit) const; // Annual energy-balance habitat, independent of view time.
    Color color(const Sample& sample) const;
    Vec3 findLand() const;
private:
    friend class RiverNetwork;
    friend class LandscapeAtlas;
    Relief rawRelief(Vec3 unit) const;
    Sample evaluate(Vec3 unit, double seconds, bool annualMean) const;
    struct Plate { Vec3 center; Vec3 velocity; double continental; };
    Config config_;
    Modules modules_;
    Noise noise_;
    std::array<Plate, 24> plates_{};
    std::array<double, 181> annualSolar_{};
    mutable std::optional<std::uint64_t> caveCacheId_;
    mutable std::optional<CaveSite> caveCache_;
    mutable std::shared_ptr<const RiverNetwork> waterNetwork_;
    mutable bool waterLandforms_=false,waterTerrain_=false;
    mutable std::shared_ptr<const LandscapeAtlas> landscape_;
    mutable bool landscapeLandforms_=false;
};
} // namespace planet

#pragma once
#include "planet/lighting.hpp"
#include "planet/weather.hpp"

namespace planet {
class Simulation;
enum class Phenomenon { Natural, Rainbow, Halo, Pillar, Aurora, DistantLightning, Fog, Dust };
std::string_view phenomenonName(Phenomenon kind);
std::optional<Phenomenon> phenomenonType(std::string_view name);
double dewPoint(double temperatureK,double relativeHumidity);
double primaryRainbowRadius(double refractiveIndex);
double haloRadius(double refractiveIndex);
Vec3 magneticPole(std::uint64_t seed);
double magneticLatitude(Vec3 observer,std::uint64_t seed);
struct DistantFlash {Vec3 direction{};double intensity=0;};
struct PhenomenaForcing {
    bool enabled=true,clouds=true,wind=true;
    double temperature=288.15,humidity=.5,windSpeed=0,rainMmH=0;
    double cloudWater=0,sunCloudWater=0,upperIce=0,lowIce=0;
    double wetness=0,dustSource=0,snowDepth=0,aboveGround=1.75;
    double geomagneticActivity=.35;
    Wind2 velocity{};
    std::vector<DistantFlash> flashes;
};
struct PhenomenaState {
    bool enabled=false;
    double rainbow=0,halo=0,pillar=0,aurora=0,fog=0,dust=0;
    double visibility=100000,dewPointK=273.15,magneticLatitude=0,activity=0;
    Color haze{.68f,.73f,.76f};
    std::vector<DistantFlash> flashes;
};
PhenomenaForcing phenomenaForcing(const World& world,const Simulation& simulation,Vec3 observer,double altitude,double seconds,
                                Phenomenon experiment=Phenomenon::Natural,double experimentSeconds=0);
PhenomenaState atmosphericPhenomena(const Config& config,Vec3 observer,double seconds,const PhenomenaForcing& forcing);
struct OpticalVertex {Vec3 direction;Color color;float alpha;};
// Unit directions, complete triangles, bounded geometry; terrain occludes the sky pass.
std::vector<OpticalVertex> atmosphericSky(const Config& config,Vec3 observer,double altitude,double seconds,double animationSeconds,const PhenomenaState& state);
struct DustParticle {Vec3 position;double radius,opacity;};
std::vector<DustParticle> dustParticles(const Config& config,Vec3 observer,double ground,double seconds,Wind2 wind,double strength);
struct PhenomenonVisit {Vec3 position,look;double seconds;};
PhenomenonVisit findPhenomenon(const World& world,Phenomenon kind,double referenceSeconds);
} // namespace planet

#pragma once
#include "planet/model.hpp"
#include <vector>

namespace planet {
struct SkyConditions {
    bool atmosphere=true;
    double altitude=0,cloudWater=0,sunCloudWater=0,rainMmH=0;
};
struct Lighting {
    Vec3 sun{},up{};
    double elevation=0,horizon=0,angularRadius=0,daylight=0,twilight=0,night=0;
    double direct=0,cloudCover=0,starVisibility=0,starlightLux=0,airDensity=1;
    Color sunlight{},ambient{},zenith{},horizonColor{},cloudColor{};
};
double siderealAngle(const Config& config,double seconds);
Vec3 inertialToPlanet(Vec3 direction,const Config& config,double seconds);
double relativeAirMass(double elevationRadians);
Lighting lighting(const Config& config,Vec3 observer,double seconds,const SkyConditions& conditions={});
Color skyColor(const Lighting& light,Vec3 viewDirection);
struct Star { Vec3 inertialDirection; double magnitude; Color color; };
std::vector<Star> starCatalog(std::uint64_t seed,unsigned count=4096);
double solarHourTime(const Config& config,Vec3 observer,double referenceSeconds,double hour);
} // namespace planet

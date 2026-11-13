#pragma once
#include "planet/hydrology.hpp"

namespace planet {
// RGBA channels and weights always use rock, soil, sand, snow order.
struct SurfaceMaterial {
    std::array<float,4> weights{1,0,0,0};
    float wetness=0,backgroundWetness=0;
};
struct MaterialSample { Color albedo; SurfaceMaterial surface; };
MaterialSample surfaceMaterial(const Sample& sample,Vec3 worldPosition,double upNormal,
                               double snowDepth,const GroundSample& ground,std::uint64_t seed,
                               bool biomes=true,bool sheltered=false);
double materialWetness(double saturation,double waterDepth);
Color wetMaterialColor(Color dry,const SurfaceMaterial& material);
double materialRoughness(const SurfaceMaterial& material);
struct MaterialMip {int size;std::vector<unsigned char> rgba;};
// A single periodic packed texture, generated once, including a complete mip chain.
std::vector<MaterialMip> makeMaterialTexture(int size=256);
constexpr double materialPeriod=8;
Vec3 materialOrigin(Vec3 planetOrigin);
} // namespace planet

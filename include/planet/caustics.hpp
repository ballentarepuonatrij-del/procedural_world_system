#pragma once
#include "planet/hydrology.hpp"

namespace planet {
// Incoming unit ray and outward unit normal. Returns zero for total internal
// reflection or a back-facing surface. Indices are relative optical indices.
Vec3 refractLight(Vec3 incoming,Vec3 normal,double fromIndex,double toIndex);
class CausticMap {
public:
    static constexpr int size=64,rays=128;
    static constexpr double tile=8,referenceDepth=3;
    CausticMap();
    // x/y tangent, z up. Light direction points toward the sun. Frozen surface
    // ripples are stationary; open-water ripples follow model time, not frames.
    void update(Vec3 sun,double seconds,double roughness,bool frozen,std::uint64_t seed);
    const std::array<float,size*size>& intensity() const {return intensity_;}
private:
    std::array<std::array<float,rays*rays>,4> cosine_,sine_;
    std::array<float,size*size> intensity_{};
};
struct WaterOpticsMap {
    static constexpr int size=Hydrology::resolution;
    // RGBA16: liquid surface height, ice coverage, transmission, wet mask.
    std::array<std::uint16_t,size*size*4> pixels{};
    double levelBase=0,levelRange=1;
    bool wet=false,hasIce=false;
};
WaterOpticsMap waterOpticsMap(const World& world,const Hydrology& water);
double causticTransmission(double depth,double iceTransmission,double turbidity,double sunHeight);
} // namespace planet

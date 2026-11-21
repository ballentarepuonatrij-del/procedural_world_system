#pragma once
#include "planet/surface_detail.hpp"
#include "planet/leaf_spray.hpp"
#include <span>

namespace planet {
struct Triangle {Vec3 a,b,c;bool twoSided=false;};
struct MeshFace {
    Triangle triangle;Color color;Vec3 flutter{};double leafPhase=0;bool microFoliage=false;
    bool castsShadow=true;
    std::array<LeafUV,3> uv{};
};
std::vector<MeshFace> detailMesh(const SurfaceObject& object,Vec3 origin,Vec3 up,Vec3 right,bool simple,bool leafDetail=false,
                                 std::vector<Triangle>* sprayShadows=nullptr);
// Closed directional volumes, with welded internal edges and near/far caps.
std::vector<Triangle> shadowVolume(std::span<const Triangle> faces,Vec3 towardSun,double extrusion=8000,double bias=.015);
} // namespace planet

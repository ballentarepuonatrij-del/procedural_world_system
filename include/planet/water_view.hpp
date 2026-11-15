#pragma once
#include "planet/terrain_mesh.hpp"

namespace planet {
// Six-plane conservative visibility. A shared nonzero bit rejects a primitive;
// vertices outside different planes can still form a visible triangle.
class ViewFrustum {
public:
    ViewFrustum(Vec3 eye,Vec3 forward,Vec3 up,double verticalFov,double aspect,double nearPlane,double farPlane);
    unsigned char outside(Vec3 position,double displacement=0) const;
private:
    Vec3 eye_,forward_,right_,up_;
    double tanX_,tanY_,marginX_,marginY_,near_,far_,eyeTolerance_;
};
struct WaterSurfacePoint {
    Vec3 base; // local position before wave displacement
    GroundSample ground;
    double depth=0;
    bool ocean=false;
    IceSample ice;
};
// Only hydrostatic data is cached. Waves, camera-dependent reflection and
// visibility remain live. Invalidate when adopting a mesh or loading a world,
// including snapshots with equal counters and vertex counts.
class WaterSurfaceCache {
public:
    void invalidate() {valid_=false;}
    bool update(const World& world,const Hydrology& water,std::span<const SurfaceWaterVertex> vertices,
        std::span<const std::array<std::size_t,3>> triangles,Vec3 anchor,Vec3 east,Vec3 north);
    const std::vector<WaterSurfacePoint>& points() const {return points_;}
    const std::vector<std::size_t>& wetVertices() const {return wetVertices_;}
    const std::vector<std::size_t>& wetTriangles() const {return wetTriangles_;}
    std::uint64_t rebuilds() const {return rebuilds_;}
private:
    std::vector<WaterSurfacePoint> points_;
    std::vector<std::size_t> wetVertices_,wetTriangles_;
    std::uint64_t revision_=0,steps_=0,seed_=0,rebuilds_=0;
    double radius_=0,sea_=0;
    Vec3 anchor_{};
    std::size_t triangles_=0;
    bool valid_=false,ocean_=false;
};
} // namespace planet

#include "planet/water_view.hpp"
#include <stdexcept>

namespace planet {
ViewFrustum::ViewFrustum(Vec3 eye,Vec3 forward,Vec3 up,double fov,double aspect,double nearPlane,double farPlane)
    : eye_(eye),forward_(normalized(forward)),right_(normalized(cross(forward_,up))),up_(cross(right_,forward_)),
      tanX_(std::tan(fov/2)*aspect),tanY_(std::tan(fov/2)),marginX_(std::hypot(1.,tanX_)),marginY_(std::hypot(1.,tanY_)),near_(nearPlane),far_(farPlane),eyeTolerance_(length(eye)*1e-6) {
    if(!std::isfinite(length(eye)) || !std::isfinite(length(forward)) || !std::isfinite(length(up)) ||
       length(forward)<1e-8 || length(cross(forward,up))<1e-8 || !std::isfinite(fov) || fov<=0 || fov>=pi ||
       !std::isfinite(aspect) || aspect<=0 || !std::isfinite(near_) || near_<=0 || !std::isfinite(far_) || far_<=near_)
        throw std::invalid_argument("Invalid view frustum");
}
unsigned char ViewFrustum::outside(Vec3 position,double displacement) const {
    const auto delta=position-eye_;const double x=dot(delta,right_),y=dot(delta,up_),z=dot(delta,forward_);
    // OpenGL may transform the submitted doubles in single precision. Keep a
    // small relative margin so CPU culling cannot shave off boundary pixels.
    displacement+=eyeTolerance_+(std::abs(x)+std::abs(y)+std::abs(z))*1e-6;
    const double mx=displacement*marginX_+1e-7,my=displacement*marginY_+1e-7;
    return static_cast<unsigned char>((x < -z*tanX_-mx ? 1 : 0)|(x > z*tanX_+mx ? 2 : 0)|
        (y < -z*tanY_-my ? 4 : 0)|(y > z*tanY_+my ? 8 : 0)|
        (z < near_-displacement-1e-7 ? 16 : 0)|(z > far_+displacement+1e-7 ? 32 : 0));
}
bool WaterSurfaceCache::update(const World& world,const Hydrology& water,std::span<const SurfaceWaterVertex> vertices,
    std::span<const std::array<std::size_t,3>> triangles,Vec3 anchor,Vec3 east,Vec3 north) {
    const auto& c=world.config();const bool ocean=world.modules().enabled(Module::Ocean);
    if(valid_ && revision_==water.revision() && steps_==water.steps() && seed_==c.seed && radius_==c.radius && sea_==c.seaLevel &&
       ocean_==ocean && points_.size()==vertices.size() && triangles_==triangles.size() && length(anchor_-anchor)==0) return false;
    valid_=false;points_.assign(vertices.size(),{});wetVertices_.clear();wetTriangles_.clear();
    const auto origin=anchor*c.radius;
    for(std::size_t i=0;i<vertices.size();++i) {
        const auto& v=vertices[i];if(!v.available) continue;
        auto& p=points_[i];p.ground=water.sample(v.up);const auto& g=p.ground;
        p.ocean=!v.river && !v.inland && ocean && (v.altitude<c.seaLevel || !g.available || g.depth+g.frozenDepth<.0005);
        p.depth=v.river ? -.01 : v.inland ? v.level-v.altitude : p.ocean ? c.seaLevel-v.altitude : (g.available ? (g.depth+g.frozenDepth)*g.weight : 0);
        const double level=p.ocean ? c.seaLevel : v.altitude+p.depth;
        p.ice=water.iceAt(v.up,p.depth,level,p.ocean);
        const auto position=v.up*(c.radius+level+p.ice.freeboard)-origin;
        p.base={dot(position,east),dot(position,anchor),dot(position,north)};
    }
    std::vector<unsigned char> used(vertices.size(),0);
    for(std::size_t i=0;i<triangles.size();++i) {
        const auto& t=triangles[i];
        if(t[0]>=vertices.size() || t[1]>=vertices.size() || t[2]>=vertices.size()) throw std::invalid_argument("Invalid water triangle");
        if(points_[t[0]].depth<=.0005 && points_[t[1]].depth<=.0005 && points_[t[2]].depth<=.0005) continue;
        wetTriangles_.push_back(i);
        for(const auto j:t) if(!used[j]) {used[j]=1;wetVertices_.push_back(j);}
    }
    revision_=water.revision();steps_=water.steps();seed_=c.seed;radius_=c.radius;sea_=c.seaLevel;
    ocean_=ocean;anchor_=anchor;triangles_=triangles.size();valid_=true;++rebuilds_;return true;
}
} // namespace planet

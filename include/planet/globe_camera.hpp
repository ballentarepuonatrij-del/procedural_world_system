#pragma once
#include "planet/math.hpp"
#include <optional>

namespace planet {
// Unit-sphere camera; renderer and cursor picking share the projection.
struct GlobeCamera {
    static constexpr double distance=3.25,fov=radians(44);
    Vec3 outward{1,0,0},up{0,1,0};
    void center(Vec3 observer);
    void drag(double dx,double dy);
    Vec3 right() const { return normalized(cross(-outward,up)); }
    std::optional<Vec3> pick(double x,double y,int width,int height) const;
};
} // namespace planet

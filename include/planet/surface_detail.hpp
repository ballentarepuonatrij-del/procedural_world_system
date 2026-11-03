#pragma once
#include "planet/model.hpp"
#include <vector>

namespace planet {
enum class DetailKind { Rock, Tree, Shrub, Grass };
struct SurfaceObject {
    std::uint64_t id{};
    DetailKind kind{};
    Vec3 up{};
    double elevation{}, height{}, radius{}, angle{};
    bool evergreen{};
    double collisionRadius() const;
    double collisionHeight() const;
};
// A finite spherical neighbourhood. Coordinates, identity and shape depend on
// seed and fixed cube-face cells, never on the observer or query order.
// Radius is limited to 600 m to bound work and memory; ground cover to 80 m.
std::vector<SurfaceObject> surfaceObjects(const World& world, Vec3 center,
                                        double radius, bool groundCover=true);
} // namespace planet

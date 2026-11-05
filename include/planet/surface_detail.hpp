#pragma once
#include "planet/flora.hpp"
#include <vector>

namespace planet {
enum class DetailKind { Rock, Tree, Shrub, Grass };
struct SurfaceObject {
    std::uint64_t id{};
    DetailKind kind{};
    Vec3 up{};
    double elevation{}, height{}, radius{}, angle{};
    bool evergreen{};
    Species species=Species::None;
    double suitability=0;
    PlantState plant;
    FlowerCycle flowerCycle;
    FlowerState flowers;
    double collisionRadius() const;
    double collisionHeight() const;
};
// A finite spherical neighbourhood. Coordinates, identity and shape depend on
// seed and fixed cube-face cells, never on the observer or query order.
// Radius is limited to 600 m to bound work and memory; ground cover to 80 m.
std::vector<SurfaceObject> surfaceObjects(const World& world, Vec3 center,
                                        double radius, bool groundCover=true,const Flora* flora=nullptr);
// Fixed sites include dormant seeds. Geometry caches can age these without
// regenerating the planet or moving roots as the observer moves.
std::vector<SurfaceObject> surfaceSites(const World& world,Vec3 center,double radius,bool groundCover=true);
SurfaceObject grownObject(SurfaceObject site,const Flora* flora=nullptr);
Landmark findGrove(const World& world);
Landmark findMeadow(const World& world);
} // namespace planet

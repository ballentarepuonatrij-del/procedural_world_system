#pragma once
#include "planet/model.hpp"

namespace planet {
bool inlandLandform(Landform kind);
bool coastalLandform(Landform kind);
// A fixed, seed-derived geomorphic site. Water levels are prescribed reference
// reservoirs; generating a basin is not a simulation of geological evolution.
struct LandscapePatch {
    std::uint64_t id=0;
    Landform kind=Landform::Lowland;
    Vec3 anchor{},right{},forward{};
    double radius=0,width=0,length=0,level=0,depth=0;
    Vec3 local(Vec3 p) const;
    Vec3 directionAt(double x,double z) const;
    double outline(Vec3 p) const;
    bool affects(Vec3 p) const;
    Relief apply(Vec3 p,Relief original) const;
    CaveSite cave() const;
};
class LandscapeAtlas {
public:
    explicit LandscapeAtlas(const World& world);
    std::optional<LandscapePatch> patch(Vec3 p) const;
    Relief apply(Vec3 p,Relief original,bool inland,bool coast) const;
    Landmark find(Landform kind,Vec3 around) const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace planet

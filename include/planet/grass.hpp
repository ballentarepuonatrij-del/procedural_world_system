#pragma once
#include "planet/flora.hpp"
#include <array>
#include <functional>
#include <span>
#include <vector>

namespace planet {
struct GrassGround {
    Vec3 base; // rendered position in the terrain snapshot's local frame
    Sample sample;
    double slope=0,snow=0;
    bool supported=true;
    Vec3 normal{0,1,0};
};
struct GrassRoot {
    std::uint64_t id=0;
    Vec3 up,base;
    double elevation=0,snow=0,height=0,width=0,suitability=0,phase=0;
    Color color{};
    bool tall=false;
    Vec3 normal{0,1,0};
};
constexpr double grassRadius=48;
constexpr std::size_t maximumGrassRoots=8192,maximumGrassVertices=180000;
double grassCoverage(const GrassGround& ground,double seaLevel);
std::vector<GrassRoot> makeGrassRoots(const World& world,Vec3 observer,const std::function<GrassGround(Vec3)>& sample);
struct GrassVertex {Vec3 position,normal,root;Color color;float flex=0,height=0,phase=0,tall=0;};
struct GrassClump {std::size_t first=0,count=0;Vec3 up,base;double height=0;bool tall=false;};
struct GrassMesh {std::vector<GrassVertex> vertices;std::vector<GrassClump> clumps;std::size_t shortCount=0,tallCount=0;};
GrassMesh makeGrassMesh(std::span<const GrassRoot> roots,Vec3 observer,double radius,const Flora& flora);

struct GrassSpring {Vec3 bend{},velocity{};};
void advanceGrassSpring(GrassSpring& spring,Vec3 target,double seconds,double frequency);
Vec3 grassWindTarget(Vec3 wind,bool tall);
Vec3 bendGrassPosition(const GrassVertex& vertex,Vec3 bend,double visibility=1);
struct GrassCollider {Vec3 previous,position;double radius=.45,height=1.8;}; // world metres; swept upright capsule
// Local reversible disturbance only. Fixed world cells retain spring state
// across small moves; teleports/seed changes clear it. Does not alter world saves.
class GrassField {
public:
    static constexpr int resolution=64;
    static constexpr double spacing=1;
    void clear();
    void synchronize(const Config& config,Vec3 observer,std::span<const GrassRoot> roots,bool refreshGround=false);
    void advance(double seconds,std::span<const GrassCollider> colliders);
    Vec3 sample(Vec3 worldPosition) const;
    Vec3 local(Vec3 worldPosition) const;
    std::array<unsigned short,resolution*resolution*2> texture() const;
    Vec3 anchor() const {return anchor_;}
    Vec3 eastAxis() const {return east_;}
    Vec3 northAxis() const {return north_;}
    double radius() const {return radius_;}
    double originX() const {return originX_*spacing;}
    double originZ() const {return originZ_*spacing;}
    std::uint64_t revision() const {return revision_;}
    std::size_t activeCells() const {return active_;}
    double maximumBend() const;
private:
    struct Cell {GrassSpring spring;double ground=0,weight=0;};
    std::array<Cell,resolution*resolution> cells_{};
    Vec3 anchor_{},east_{},north_{};
    double radius_=1;
    int originX_=0,originZ_=0;
    std::uint64_t seed_=0,revision_=0;
    std::size_t active_=0;
    bool initialized_=false;
};
} // namespace planet

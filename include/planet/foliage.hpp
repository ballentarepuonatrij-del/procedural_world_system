#pragma once
#include "planet/grass.hpp"
#include "planet/leaf_spray.hpp"

namespace planet {
struct SurfaceObject;
constexpr std::size_t maximumFoliagePlants=256,maximumLeafVertices=30000;
constexpr double foliageMotionRadius=128,foliageContactRadius=32;
struct FoliageRoot {
    std::uint64_t id=0,generation=0;
    Vec3 position,up;
    double height=0,radius=0,lower=0,compliance=1,phase=0;
};
struct FoliageVertex {
    Vec3 position,normal;Color color;
    float flex=0,height=0,phase=0,flutter=0,leafPhase=0,slot=-1;
    LeafUV uv{};
};
struct FoliageRange {std::size_t first=0,count=0;int slot=-1;};
constexpr std::size_t foliageBatchVertices=1536;
struct FoliageBatch {
    Vec3 center;
    double radius=0,flexHeight=0,flutter=0;
    std::size_t count=0;
    double sweptRadius(double bend,double windStrength) const;
};
std::vector<FoliageBatch> foliageBatches(std::span<const FoliageVertex> vertices);
bool flexiblePlant(const SurfaceObject& object);
FoliageRoot foliageRoot(const SurfaceObject& object,double radius);
Vec3 foliageWindTarget(const FoliageRoot& root,Vec3 wind);
Vec3 bendFoliagePosition(const FoliageVertex& vertex,Vec3 bend,double phase,double windStrength);
// At most 256 springs and 32 swept capsules. State is transient and keyed by
// the fixed plant identity/generation, so scene streaming cannot move roots.
class FoliageField {
public:
    void clear();
    void synchronize(std::span<const FoliageRoot> roots);
    void advance(double modelSeconds,double contactSeconds,Vec3 wind,Vec3 eye,std::span<const GrassCollider> colliders);
    Vec3 bend(std::size_t slot) const;
    std::span<const FoliageRoot> roots() const {return roots_;}
    std::size_t contacts() const {return contacts_;}
    double maximumContact() const;
    double maximumBend() const;
private:
    struct State {GrassSpring wind,contact;};
    std::vector<FoliageRoot> roots_;
    std::vector<State> states_;
    std::size_t contacts_=0;
};
} // namespace planet

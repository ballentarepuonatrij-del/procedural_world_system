#pragma once
#include "planet/shadows.hpp"
#include <array>

namespace planet {
enum class DeerActivity { Grazing, Walking, Resting, Drinking, Fleeing };
std::string_view deerActivityName(DeerActivity activity);
struct DeerHabitat {
    std::uint64_t id=0;
    Vec3 home{},bank{},water{},outward{};
};
struct Deer {
    std::uint64_t id=0;
    Vec3 up{},previous{},heading{},previousHeading{},target{};
    double elevation=0,previousElevation=0,speed=0,stride=0,previousStride=0;
    double lowerHead=0,previousHead=0,foldLegs=0,previousFold=0,size=1;
    bool male=false;
    DeerActivity activity=DeerActivity::Grazing;
};
struct DeerHerd {
    DeerHabitat habitat;
    std::vector<Deer> members;
    std::vector<SurfaceObject> obstacles;
    Vec3 obstacleCenter{};
    DeerActivity activity=DeerActivity::Grazing;
    double remaining=0,thirst=.5,alarm=0;
    std::uint64_t decisions=0,lastSeen=0;
    bool active=false;
};
struct DeerPose {
    std::uint64_t id=0,herd=0;
    Vec3 up{},heading{};
    double elevation=0,stride=0,lowerHead=0,foldLegs=0,size=1,speed=0;
    bool male=false;
    DeerActivity activity=DeerActivity::Grazing;
};
// Fixed resource sites, bounded active population. Unobserved herds are dormant;
// this is local animal behaviour, not a global population/ecosystem model.
class Fauna {
public:
    static constexpr double fixedStep=.1,activeRadius=650,drawRadius=400;
    static constexpr std::size_t maximumHerds=3,cacheLimit=32;
    void advance(const World& world,Vec3 observer,double seconds,double observerAltitude,bool observable=true,bool threat=true);
    bool focus(const World& world,const DeerHabitat& site);
    std::vector<DeerPose> visible(Vec3 observer,double radius=drawRadius) const;
    const std::vector<DeerHerd>& herds() const {return herds_;}
    double elapsed() const {return elapsed_;}
    std::uint64_t steps() const {return steps_;}
    bool enabled() const {return enabled_;}
private:
    void synchronize(const World& world);
    void discover(const World& world,Vec3 observer);
    void tick(const World& world,Vec3 observer,double observerAltitude,bool threat);
    std::vector<DeerHerd> herds_;
    Config config_{};
    Vec3 surveyed_{};
    double pending_=0,elapsed_=0;
    std::uint64_t steps_=0,visit_=0;
    unsigned geometry_=0;
    bool initialized_=false,enabled_=false;
};
DeerHabitat findDeerHabitat(const World& world,Vec3 near);
std::vector<MeshFace> deerMesh(const DeerPose& pose,bool simple=false);
} // namespace planet

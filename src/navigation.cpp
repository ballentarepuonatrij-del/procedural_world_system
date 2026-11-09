#include "planet/navigation.hpp"
#include "planet/hydrology.hpp"
#include <stdexcept>

namespace planet {
Explorer::Explorer(const World& world) { teleport(world,world.findLand()); }
void Explorer::teleport(const World& world, Vec3 position,const Hydrology* water) {
    up_ = normalized(position); heading_ = north(up_); pitch_ = 0;
    altitude_ = (water ? water->ground(world,up_) : world.surface(up_)) + eyeHeight;
    if(water) altitude_+=water->snow.walkingDepth(up_);
    verticalSpeed_ = 0; grounded_ = true; jumpHeld_ = false;
    underground_=false; cave_=world.caveSite(up_);
    const auto liquid=liquidAt(world,water,nullptr,up_);
    swimming_=liquid.present && liquid.depth>eyeHeight; underwater_=false; swimVelocity_={};
    if(swimming_) { altitude_=liquid.level+0.22; grounded_=false; }
    obstacleAnchor_={};
}
void Explorer::dive(const World& world,Vec3 direction,double depth,const Hydrology* water,const WaterMotion* motion) {
    if(!std::isfinite(depth) || depth<0 || depth>200) throw std::invalid_argument("Dive depth must be in [0,200] m");
    teleport(world,direction,water); const auto liquid=liquidAt(world,water,motion,up_);
    if(liquid.present) altitude_=std::max(liquid.bed+eyeHeight,liquid.level-depth);
    update(world,{},0.001,water,motion);
}
void Explorer::look(double horizontal, double vertical) {
    heading_ = normalized(rotate(heading_,up_,-horizontal));
    pitch_ = clamp(pitch_+vertical,-radians(88),radians(88));
}
void Explorer::update(const World& world, const Input& input, double dt,const Hydrology* water,const WaterMotion* motion,const Flora* flora) {
    if (!std::isfinite(dt) || dt < 0 || dt > 10) throw std::invalid_argument("Invalid navigation timestep");
    cave_=world.caveSite(up_);
    unsigned modules=0;
    for(unsigned i=0;i<moduleCount;++i)
        if(world.modules().enabled(static_cast<Module>(i))) modules|=1U<<i;
    if(modules!=obstacleModules_ || world.config().seed!=obstacleSeed_) {
        obstacleAnchor_={}; obstacleModules_=modules; obstacleSeed_=world.config().seed;
    }
    // Physics stays stable after slow frames. The desktop caps elapsed time at 0.1 s.
    while (dt > 1e-10) {
        const double slice = std::min(dt,1.0/120.0);
        step(world,input,slice,water,motion,flora); dt -= slice;
    }
    jumpHeld_ = input.jump;
}
void Explorer::refreshObstacles(const World& world,const Hydrology* water,const Flora* flora) {
    const double day=flora ? flora->days() : 0;
    const bool young=flora && flora->young();
    if(length(up_-obstacleAnchor_)*world.config().radius<12 && std::abs(day-obstacleFloraDay_)<.25 && young==obstacleYoung_) return;
    obstacles_=surfaceObjects(world,up_,32,false,flora);
    if(water) for(auto& object:obstacles_) object.elevation=water->ground(world,object.up);
    obstacleAnchor_=up_;
    obstacleFloraDay_=day; obstacleYoung_=young;
}
void Explorer::step(const World& world, const Input& input, double dt,const Hydrology* water,const WaterMotion* motion,const Flora* flora) {
    if(!flying) refreshObstacles(world,water,flora);
    auto liquid=liquidAt(world,water,motion,up_);
    const double immersed=liquid.present ? clamp((liquid.level-altitude_+eyeHeight)/eyeHeight,0.0,1.0) : 0;
    swimming_=!flying && liquid.depth>1.1 && immersed>0.55;
    const double diveForward=swimming_ && underwater_ ? std::sin(pitch_)*input.forward : 0;
    const double vertical=clamp(input.vertical+diveForward,-1.0,1.0);
    Vec3 tangent = heading_*(input.forward*(swimming_ && underwater_ ? std::cos(pitch_) : 1)) + right()*input.right;
    const double magnitude = length(tangent);
    if (magnitude > 1) tangent = tangent/magnitude;
    const double snowDepth=water && !underground_ ? water->snow.sample(up_).depth : 0;
    const double speed = flying ? std::min(flightSpeed*(input.sprint ? 8 : 1),cave_ ? 30.0 : 160000.0) :
        (swimming_ ? (input.sprint ? 2.6 : 1.6) : (input.sprint ? 8.0 : 2.5)/std::max(1.0,1+2*liquid.depth+2.5*snowDepth));
    Vec3 velocity=tangent*speed;
    if(swimming_) {
        if(liquid.ocean && motion) liquid.current=motion->current(up_,std::max(0.0,liquid.level-altitude_));
        const auto target=velocity+liquid.current;
        swimVelocity_=swimVelocity_+(target-swimVelocity_)*(1-std::exp(-dt/0.7));
        swimVelocity_=swimVelocity_-up_*dot(swimVelocity_,up_); velocity=swimVelocity_;
    } else swimVelocity_={};
    const Vec3 previous = up_;
    const Vec3 previousHeading = heading_;
    const double angle = length(velocity)*dt/(world.config().radius+altitude_);
    if (angle > 0) {
        const Vec3 axis = normalized(cross(up_,velocity));
        up_ = normalized(rotate(up_,axis,angle));
        heading_ = normalized(rotate(heading_,axis,angle));
    }
    if(!flying && angle>0 && !underground_) {
        for(const auto& object:obstacles_) {
            const double solid=object.collisionRadius();
            if(solid<=0 || altitude_-eyeHeight>object.elevation+object.collisionHeight() ||
               altitude_<object.elevation) continue;
            const double distance=length(up_-object.up)*world.config().radius;
            const double before=length(previous-object.up)*world.config().radius;
            // A player already inside an object (e.g. after toggling detail)
            // can always walk out. Flight deliberately bypasses object collision.
            if(distance<solid+0.3 && distance<before) {
                up_=previous; heading_=previousHeading; break;
            }
        }
    }
    struct Space { double floor,ceiling; bool roof; };
    const auto column=[&](Vec3 p) {
        const double surface=water ? water->ground(world,p) : world.elevation(p);
        Space result{surface+(water ? water->snow.walkingDepth(p) : 0),1e8,false};
        if(cave_) {
            const auto c=cave_->column(p);
            if(c.valid() && c.floor<surface) {
                if(c.ceiling>=surface) result.floor=c.floor;
                else if(underground_ || altitude_<surface-0.10) result={c.floor,c.ceiling,true};
            }
        }
        return result;
    };
    const auto spaceAt=[&](Vec3 p) {
        auto space=column(p);
        if(cave_) for(const auto offset:{east(p)*0.28,-east(p)*0.28,north(p)*0.28,-north(p)*0.28}) {
            const auto other=column(normalized(p+offset/world.config().radius));
            space.floor=std::max(space.floor,other.floor); space.ceiling=std::min(space.ceiling,other.ceiling); space.roof=space.roof || other.roof;
        }
        return space;
    };
    auto space=spaceAt(up_);
    if(angle>0 && cave_ && (space.ceiling-std::max(space.floor,altitude_-eyeHeight)<eyeHeight+0.20 ||
       (underground_ && space.floor>altitude_-eyeHeight+speed*dt*1.1+0.08))) {
        up_=previous; heading_=previousHeading; space=spaceAt(up_);
    }
    double floor=space.floor+eyeHeight;
    // Prevent walking up cliffs; airborne collisions still resolve against the new surface.
    if (!flying && (grounded_ || swimming_) && floor-altitude_ > speed*dt*1.1+0.08) {
        up_ = previous; heading_ = previousHeading;
        space=spaceAt(up_); floor=space.floor+eyeHeight;
    }
    if (flying) {
        altitude_ += input.vertical*speed*dt;
        altitude_ = clamp(altitude_,floor,100000.0);
        verticalSpeed_ = 0; grounded_ = altitude_ <= floor+0.01;
    } else if(swimming_) {
        const double buoyancy=world.config().gravity*(1.14*immersed-1);
        verticalSpeed_+=(buoyancy+vertical*5-2.4*verticalSpeed_-0.6*std::abs(verticalSpeed_)*verticalSpeed_)*dt;
        altitude_+=verticalSpeed_*dt;
        if(altitude_<floor) { altitude_=floor; verticalSpeed_=std::max(0.0,verticalSpeed_); }
        grounded_=altitude_<=floor+0.01;
    } else {
        if (input.jump && !jumpHeld_ && grounded_) {
            verticalSpeed_ = 4.5; grounded_ = false; jumpHeld_ = true;
        }
        const double g = world.config().gravity*std::pow(world.config().radius/(world.config().radius+altitude_),2);
        if (grounded_ && !input.jump && altitude_-floor < 0.25) altitude_ = floor;
        else {
            verticalSpeed_ -= g*dt;
            altitude_ += verticalSpeed_*dt;
        }
        if (altitude_ <= floor) { altitude_ = floor; verticalSpeed_ = 0; grounded_ = true; }
        else if (altitude_-floor > 0.001) grounded_ = false;
    }
    if(altitude_>space.ceiling-0.20) { altitude_=std::max(floor,space.ceiling-0.20); verticalSpeed_=std::min(0.0,verticalSpeed_); }
    underground_=space.roof && altitude_<world.surface(up_);
    liquid=liquidAt(world,water,motion,up_);
    if(!liquid.present || altitude_>liquid.level+0.04) underwater_=false;
    else if(altitude_<liquid.level-0.04) underwater_=true;
}
} // namespace planet

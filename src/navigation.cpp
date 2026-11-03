#include "planet/navigation.hpp"
#include "planet/hydrology.hpp"
#include <stdexcept>

namespace planet {
Explorer::Explorer(const World& world) { teleport(world,world.findLand()); }
void Explorer::teleport(const World& world, Vec3 position,const Hydrology* water) {
    up_ = normalized(position); heading_ = north(up_); pitch_ = 0;
    altitude_ = (water ? water->ground(world,up_) : world.surface(up_)) + eyeHeight;
    verticalSpeed_ = 0; grounded_ = true; jumpHeld_ = false;
    underground_=false; cave_=world.caveSite(up_);
    obstacleAnchor_={};
}
void Explorer::look(double horizontal, double vertical) {
    heading_ = normalized(rotate(heading_,up_,-horizontal));
    pitch_ = clamp(pitch_+vertical,-radians(88),radians(88));
}
void Explorer::update(const World& world, const Input& input, double dt,const Hydrology* water) {
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
        step(world,input,slice,water); dt -= slice;
    }
    jumpHeld_ = input.jump;
}
void Explorer::refreshObstacles(const World& world,const Hydrology* water) {
    if(length(up_-obstacleAnchor_)*world.config().radius<12) return;
    obstacles_=surfaceObjects(world,up_,32,false);
    if(water) for(auto& object:obstacles_) object.elevation=water->ground(world,object.up);
    obstacleAnchor_=up_;
}
void Explorer::step(const World& world, const Input& input, double dt,const Hydrology* water) {
    if(!flying) refreshObstacles(world,water);
    Vec3 tangent = heading_*input.forward + right()*input.right;
    const double magnitude = length(tangent);
    if (magnitude > 1) tangent = tangent/magnitude;
    const double depth=water ? water->sample(up_).depth : 0;
    const double speed = flying ? std::min(flightSpeed*(input.sprint ? 8 : 1),cave_ ? 30.0 : 160000.0) : (input.sprint ? 8.0 : 2.5)/std::max(1.0,1+2*depth);
    const Vec3 previous = up_;
    const Vec3 previousHeading = heading_;
    const double angle = length(tangent)*speed*dt/(world.config().radius+altitude_);
    if (angle > 0) {
        const Vec3 axis = normalized(cross(up_,tangent));
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
        const double surface=water ? water->ground(world,p) : world.surface(p);
        Space result{surface,1e8,false};
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
    if (!flying && grounded_ && floor-altitude_ > speed*dt*1.1+0.08) {
        up_ = previous; heading_ = previousHeading;
        space=spaceAt(up_); floor=space.floor+eyeHeight;
    }
    if (flying) {
        altitude_ += input.vertical*speed*dt;
        altitude_ = clamp(altitude_,floor,100000.0);
        verticalSpeed_ = 0; grounded_ = altitude_ <= floor+0.01;
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
}
} // namespace planet

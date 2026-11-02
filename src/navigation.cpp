#include "planet/navigation.hpp"
#include <stdexcept>

namespace planet {
Explorer::Explorer(const World& world) { teleport(world,world.findLand()); }
void Explorer::teleport(const World& world, Vec3 position) {
    up_ = normalized(position); heading_ = north(up_); pitch_ = 0;
    altitude_ = world.surface(up_) + eyeHeight;
    verticalSpeed_ = 0; grounded_ = true; jumpHeld_ = false;
}
void Explorer::look(double horizontal, double vertical) {
    heading_ = normalized(rotate(heading_,up_,-horizontal));
    pitch_ = clamp(pitch_+vertical,-radians(88),radians(88));
}
void Explorer::update(const World& world, const Input& input, double dt) {
    if (!std::isfinite(dt) || dt < 0 || dt > 10) throw std::invalid_argument("Invalid navigation timestep");
    // Physics stays stable after slow frames. The desktop caps elapsed time at 0.1 s.
    while (dt > 1e-10) {
        const double slice = std::min(dt,1.0/120.0);
        step(world,input,slice); dt -= slice;
    }
    jumpHeld_ = input.jump;
}
void Explorer::step(const World& world, const Input& input, double dt) {
    Vec3 tangent = heading_*input.forward + right()*input.right;
    const double magnitude = length(tangent);
    if (magnitude > 1) tangent = tangent/magnitude;
    const double speed = flying ? flightSpeed*(input.sprint ? 8 : 1) : (input.sprint ? 8.0 : 2.5);
    const Vec3 previous = up_;
    const Vec3 previousHeading = heading_;
    const double angle = length(tangent)*speed*dt/(world.config().radius+altitude_);
    if (angle > 0) {
        const Vec3 axis = normalized(cross(up_,tangent));
        up_ = normalized(rotate(up_,axis,angle));
        heading_ = normalized(rotate(heading_,axis,angle));
    }
    double floor = world.surface(up_)+eyeHeight;
    // Prevent walking up cliffs; airborne collisions still resolve against the new surface.
    if (!flying && grounded_ && floor-altitude_ > speed*dt*1.1+0.08) {
        up_ = previous; heading_ = previousHeading;
        floor = world.surface(up_)+eyeHeight;
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
}
} // namespace planet

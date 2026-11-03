#pragma once
#include "planet/surface_detail.hpp"

namespace planet {
class Hydrology;
struct Input { double forward{}, right{}, vertical{}; bool sprint{}, jump{}; };
class Explorer {
public:
    explicit Explorer(const World& world);
    void update(const World& world, const Input& input, double dt,const Hydrology* water=nullptr);
    void look(double horizontal, double vertical);
    void teleport(const World& world, Vec3 direction,const Hydrology* water=nullptr);
    Vec3 up() const { return up_; }
    Vec3 forward() const { return heading_*std::cos(pitch_) + up_*std::sin(pitch_); }
    Vec3 horizontalForward() const { return heading_; }
    Vec3 right() const { return normalized(cross(heading_,up_)); }
    double altitude() const { return altitude_; }
    double heading() const { return std::atan2(dot(heading_,east(up_)),dot(heading_,north(up_))); }
    double pitch() const { return pitch_; }
    bool grounded() const { return grounded_; }
    bool underground() const { return underground_; }
    bool flying = false;
    double flightSpeed = 250.0;
    static constexpr double eyeHeight = 1.75;
private:
    void step(const World& world, const Input& input, double dt,const Hydrology* water);
    void refreshObstacles(const World& world,const Hydrology* water);
    std::vector<SurfaceObject> obstacles_;
    Vec3 obstacleAnchor_{};
    std::uint64_t obstacleSeed_=0;
    unsigned obstacleModules_=~0U;
    std::optional<CaveSite> cave_;
    Vec3 up_, heading_;
    double altitude_ = 0, pitch_ = 0, verticalSpeed_ = 0;
    bool grounded_ = true, jumpHeld_ = false, underground_=false;
};
} // namespace planet

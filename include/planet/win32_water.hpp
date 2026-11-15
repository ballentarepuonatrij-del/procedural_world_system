#pragma once
#include "planet/model.hpp"
#include <memory>

namespace planet {
// One surface pass; GLSL 1.20 is optional, colored ice remains on GL 1.1.
class WaterEffects {
public:
    WaterEffects();~WaterEffects();
    void initialize(bool enabled);
    void prepare(Vec3 anchor,Vec3 eastAxis,Vec3 northAxis,Vec3 hydroAnchor,double radius,double light);
    bool begin(bool river);void end();
    const char* backend() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace planet

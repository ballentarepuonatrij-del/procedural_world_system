#pragma once
#include "planet/terrain_mesh.hpp"
#include <memory>

namespace planet {
class TerrainMaterials {
public:
    TerrainMaterials();
    ~TerrainMaterials();
    void initialize(bool enabled);
    void invalidate();
    void vertex(const TerrainVertex& vertex) const;
    void prepare(const World& world,const Hydrology& water,Vec3 anchor,Vec3 eastAxis,Vec3 northAxis,double detailStrength);
    void begin(bool enabled=true);
    void end();
    const char* backend() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace planet

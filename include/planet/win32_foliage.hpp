#pragma once
#include "planet/foliage.hpp"
#include <memory>

namespace planet {
class ViewFrustum;
struct PlantMesh;
struct FoliageDraw {Vec3 east,up,north;double phase=0,windStrength=0;bool enabled=true;};
class FoliageRenderer {
public:
    FoliageRenderer();~FoliageRenderer();
    void initialize(bool shaders=true);
    void vertex(const FoliageVertex& vertex,bool animated=true) const;
    void adopt(PlantMesh& mesh,unsigned staticFirst,unsigned movingFirst);
    void prepare(const FoliageField& field,const FoliageDraw& view,const ViewFrustum& frustum);
    void draw(int wire=0);
    const char* backend() const;
    std::size_t leafVertices() const;
    std::size_t animatedVertices() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace planet

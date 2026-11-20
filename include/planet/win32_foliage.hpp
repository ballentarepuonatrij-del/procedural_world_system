#pragma once
#include "planet/foliage.hpp"
#include <memory>

namespace planet {
struct FoliageDraw {Vec3 east,up,north;double phase=0,windStrength=0;bool enabled=true;};
class FoliageRenderer {
public:
    FoliageRenderer();~FoliageRenderer();
    void initialize(bool shaders=true);
    void vertex(const FoliageVertex& vertex) const;
    void adopt(std::vector<FoliageVertex> vertices,std::vector<FoliageRange> ranges,std::size_t leaves);
    void prepare(const FoliageField& field,const FoliageDraw& view);
    void draw(unsigned list,int wire=0);
    const char* backend() const;
    std::size_t leafVertices() const;
    std::size_t animatedVertices() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace planet

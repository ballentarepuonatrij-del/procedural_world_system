#pragma once
#include "planet/grass.hpp"
#include <memory>

namespace planet {
struct GrassDraw {
    Vec3 origin,east,up,north,eye,windShort,windTall;
    double phase=0,visibility=1;
    bool wireframe=false;
};
class GrassRenderer {
public:
    GrassRenderer();~GrassRenderer();
    void initialize(bool shaders=true);
    void vertex(const GrassVertex& vertex) const;
    void adopt(GrassMesh mesh);
    void draw(unsigned list,const GrassField& field,const GrassDraw& view);
    const char* backend() const;
    std::size_t shortCount() const;
    std::size_t tallCount() const;
    std::size_t vertices() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace planet

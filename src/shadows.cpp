#include "planet/shadows.hpp"
#include <map>
#include <stdexcept>

namespace planet {
std::vector<Triangle> shadowVolume(std::span<const Triangle> faces,Vec3 sunlight,double extrusion,double bias) {
    if(!std::isfinite(length(sunlight)) || length(sunlight)<1e-9 || !std::isfinite(extrusion) || extrusion<=0 ||
       !std::isfinite(bias) || bias<0) throw std::invalid_argument("Invalid shadow projection");
    const auto sun=normalized(sunlight);
    std::map<std::array<std::int64_t,3>,std::size_t> weld;
    std::vector<Vec3> vertices;
    std::map<std::pair<std::size_t,std::size_t>,int> edges;
    std::vector<Triangle> volume;volume.reserve(faces.size()*3);
    const auto index=[&](Vec3 p) {
        if(!std::isfinite(length(p)) || length(p)>1e7) throw std::invalid_argument("Invalid shadow vertex");
        const std::array key{static_cast<std::int64_t>(std::llround(p.x*1e6)),static_cast<std::int64_t>(std::llround(p.y*1e6)),static_cast<std::int64_t>(std::llround(p.z*1e6))};
        // Put the cap just inside the caster, so its lit surface is outside its own volume.
        const auto [it,added]=weld.emplace(key,vertices.size());if(added) vertices.push_back(p-sun*bias);
        return it->second;
    };
    const auto far=[&](std::size_t i) {return vertices[i]-sun*extrusion;};
    for(auto face:faces) {
        const auto n=cross(face.b-face.a,face.c-face.a);
        const double facing=dot(n,sun);if(length(n)<1e-10 || std::abs(facing)<1e-12) continue;
        if(facing<0) {if(!face.twoSided) continue;std::swap(face.b,face.c);}
        const std::array ids{index(face.a),index(face.b),index(face.c)};
        volume.push_back({vertices[ids[0]],vertices[ids[1]],vertices[ids[2]]});
        volume.push_back({far(ids[2]),far(ids[1]),far(ids[0])});
        for(unsigned j=0;j<3;++j) {const auto a=ids[j],b=ids[(j+1)%3];edges[{std::min(a,b),std::max(a,b)}]+=a<b ? 1 : -1;}
    }
    for(const auto& [edge,count]:edges) if(count) {
        const auto a=count>0 ? edge.first : edge.second,b=count>0 ? edge.second : edge.first;
        for(int i=0;i<std::abs(count);++i) {
            volume.push_back({vertices[b],vertices[a],far(a)});volume.push_back({vertices[b],far(a),far(b)});
        }
    }
    return volume;
}
} // namespace planet

#include "planet/shadows.hpp"
#include <unordered_map>
#include <stdexcept>

namespace planet {
std::vector<Triangle> shadowVolume(std::span<const Triangle> faces,Vec3 sunlight,double extrusion,double bias) {
    if(!std::isfinite(length(sunlight)) || length(sunlight)<1e-9 || !std::isfinite(extrusion) || extrusion<=0 ||
       !std::isfinite(bias) || bias<0) throw std::invalid_argument("Invalid shadow projection");
    const auto sun=normalized(sunlight);
    struct VertexHash {
        std::size_t operator()(const std::array<std::int64_t,3>& key) const {
            return static_cast<std::size_t>(hash64(static_cast<std::uint64_t>(key[0]))^
                hash64(static_cast<std::uint64_t>(key[1])+0x9e3779b97f4a7c15ULL)^
                hash64(static_cast<std::uint64_t>(key[2])+0x517cc1b727220a95ULL));
        }
    };
    std::unordered_map<std::array<std::int64_t,3>,std::size_t,VertexHash> weld;weld.reserve(faces.size());
    std::vector<Vec3> vertices;
    vertices.reserve(faces.size());
    struct Edge {std::pair<std::size_t,std::size_t> key;int winding;};
    std::vector<Edge> edges;edges.reserve(faces.size()*3);
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
        for(unsigned j=0;j<3;++j) {const auto a=ids[j],b=ids[(j+1)%3];edges.push_back({{std::min(a,b),std::max(a,b)},a<b ? 1 : -1});}
    }
    // Contiguous sorting replaces one tree allocation per edge. Keep the old
    // sorted edge order so even overlapping volumes remain deterministic.
    std::sort(edges.begin(),edges.end(),[](const Edge& a,const Edge& b){return a.key<b.key;});
    for(std::size_t i=0;i<edges.size();) {
        const auto edge=edges[i].key;int count=0;
        do {count+=edges[i++].winding;} while(i<edges.size() && edges[i].key==edge);
        if(!count) continue;
        const auto a=count>0 ? edge.first : edge.second,b=count>0 ? edge.second : edge.first;
        for(int side=0;side<std::abs(count);++side) {
            volume.push_back({vertices[b],vertices[a],far(a)});volume.push_back({vertices[b],far(a),far(b)});
        }
    }
    return volume;
}
} // namespace planet

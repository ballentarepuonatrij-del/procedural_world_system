#include "planet/caustics.hpp"
#include <stdexcept>

namespace planet {
namespace {
constexpr std::array<std::array<double,4>,4> ripples{{{4,1,.075,1.17},{1,5,.046,-1.53},{-3,4,.038,.91},{2,-2,.045,-.73}}};
}
Vec3 refractLight(Vec3 incoming,Vec3 normal,double fromIndex,double toIndex) {
    const double cosine=-dot(incoming,normal),eta=fromIndex/toIndex;
    const double k=1-eta*eta*(1-cosine*cosine);
    if(cosine<=0 || k<0) return {};
    return incoming*eta+normal*(eta*cosine-std::sqrt(k));
}
CausticMap::CausticMap() {
    for(std::size_t w=0;w<ripples.size();++w) for(int y=0;y<rays;++y) for(int x=0;x<rays;++x) {
        const double phase=tau*(ripples[w][0]*(x+.5)+ripples[w][1]*(y+.5))/rays;
        const auto i=static_cast<std::size_t>(y*rays+x);cosine_[w][i]=static_cast<float>(std::cos(phase));sine_[w][i]=static_cast<float>(std::sin(phase));
    }
}
void CausticMap::update(Vec3 sun,double seconds,double roughness,bool frozen,std::uint64_t seed) {
    if(!std::isfinite(seconds) || !std::isfinite(roughness) || roughness<0 || roughness>2 || !std::isfinite(length(sun)) || length(sun)<.5)
        throw std::invalid_argument("Invalid caustic light or ripple forcing");
    intensity_.fill(0);sun=normalized(sun);if(sun.z<=.05) return;
    std::array<double,4> cosTime{},sinTime{};
    for(std::size_t w=0;w<ripples.size();++w) {
        const double phase=tau*randomUnit(hash64(seed+w))+(frozen ? 0 : std::remainder(seconds*ripples[w][3],tau));
        cosTime[w]=std::cos(phase);sinTime[w]=std::sin(phase);
    }
    const auto flat=refractLight(-sun,{0,0,1},1,1.333);
    const double flatX=flat.x/-flat.z*referenceDepth,flatY=flat.y/-flat.z*referenceDepth;
    constexpr float energy=static_cast<float>(size*size)/static_cast<float>(rays*rays);
    for(int y=0;y<rays;++y) for(int x=0;x<rays;++x) {
        const auto i=static_cast<std::size_t>(y*rays+x);double dx=0,dy=0;
        for(std::size_t w=0;w<ripples.size();++w) {
            const double gradient=(cosine_[w][i]*cosTime[w]-sine_[w][i]*sinTime[w])*ripples[w][2]*tau/tile*roughness;
            dx+=gradient*ripples[w][0];dy+=gradient*ripples[w][1];
        }
        const auto normal=normalized(Vec3{-dx,-dy,1});
        auto ray=refractLight(-sun,normal,1,frozen ? 1.31 : 1.333);
        if(ray.z>=-.001) continue;
        double shiftX=0,shiftY=0,depth=referenceDepth;
        if(frozen) {
            constexpr double slab=.15;shiftX=ray.x/-ray.z*slab;shiftY=ray.y/-ray.z*slab;depth-=slab;
            ray=refractLight(ray,{0,0,1},1.31,1.333);
        }
        if(ray.z>=-.001) continue;
        // Remove uniform translation here; the receiving shader applies the
        // Snell translation at its actual depth. Every ray splats equal energy.
        const double u=((x+.5)*tile/rays+shiftX+ray.x/-ray.z*depth-flatX)*size/tile-.5;
        const double v=((y+.5)*tile/rays+shiftY+ray.y/-ray.z*depth-flatY)*size/tile-.5;
        const int ix=static_cast<int>(std::floor(u)),iy=static_cast<int>(std::floor(v));const double tx=u-ix,ty=v-iy;
        for(int j=0;j<2;++j) for(int k=0;k<2;++k)
            intensity_[static_cast<std::size_t>(((iy+j)%size+size)%size*size+((ix+k)%size+size)%size)]+=energy*static_cast<float>((k ? tx : 1-tx)*(j ? ty : 1-ty));
    }
}
WaterOpticsMap waterOpticsMap(const World& world,const Hydrology& water) {
    WaterOpticsMap result;double lo=1e9,hi=-1e9;
    const auto enabled=[&](const GroundCell& c) {return !c.masked && ((c.sea && world.modules().enabled(Module::Ocean)) ||
        (c.reservoir && world.modules().enabled(c.flowSpeed>0 ? Module::Rivers : Module::InlandWaters)) ||
        (!c.sea && !c.reservoir && water.enabled() && c.water+c.ice.frozen>.002));};
    const auto level=[&](const GroundCell& c) {return c.sea ? world.config().seaLevel-c.ice.frozen : c.reservoir ? c.reservoirLevel-c.ice.frozen : c.bed()+c.water;};
    for(const auto& c:water.grid().cells) if(enabled(c)) {lo=std::min(lo,level(c));hi=std::max(hi,level(c));result.wet=true;}
    if(!result.wet) return result;
    result.levelBase=lo;result.levelRange=std::max(1.,hi-lo);
    for(std::size_t i=0;i<water.grid().cells.size();++i) {
        const auto& c=water.grid().cells[i];if(!enabled(c)) continue;
        const auto ice=water.iceEnabled() ? iceProperties(c.ice.frozen,c.sea ? 35 : 0) : IceSample{};
        const double sediment=c.water>1e-6 ? c.sediment/c.water : 0;
        const auto pack=[](double value){return static_cast<std::uint16_t>(std::lround(clamp(value,0.,1.)*65535));};
        result.pixels[4*i]=pack((level(c)-lo)/result.levelRange);result.pixels[4*i+1]=pack(ice.cover);
        result.pixels[4*i+2]=pack(ice.transmission*std::exp(-sediment*.12));result.pixels[4*i+3]=65535;
        result.hasIce=result.hasIce || ice.cover>0;
    }
    return result;
}
double causticTransmission(double depth,double iceTransmission,double turbidity,double sunHeight) {
    return depth<=0 ? 0 : smoothstep(0,.12,depth)*(1-smoothstep(12,24,depth))*std::exp(-depth*(.10+std::max(0.,turbidity)*.12))*
        clamp(iceTransmission,0.,1.)*smoothstep(.05,.3,sunHeight);
}
} // namespace planet

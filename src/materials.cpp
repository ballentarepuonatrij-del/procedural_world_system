#include "planet/materials.hpp"
#include <stdexcept>

namespace planet {
namespace {
Color blend(Color a,Color b,double t) {
    return {static_cast<float>(mix(a.r,b.r,t)),static_cast<float>(mix(a.g,b.g,t)),static_cast<float>(mix(a.b,b.b,t))};
}
// Periodic value noise: both the value and its first derivative meet at the tile edge.
double tileNoise(double x,double y,int cells,std::uint64_t seed) {
    x*=cells;y*=cells;const auto ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
    const auto at=[&](int a,int b) {return randomUnit(seed+static_cast<unsigned>((a%cells+cells)%cells)*15731ULL+static_cast<unsigned>((b%cells+cells)%cells)*789221ULL);};
    return mix(mix(at(ix,iy),at(ix+1,iy),smoothstep(0,1,x-ix)),
               mix(at(ix,iy+1),at(ix+1,iy+1),smoothstep(0,1,x-ix)),smoothstep(0,1,y-iy));
}
}
double materialWetness(double saturation,double waterDepth) {
    return std::max(smoothstep(.48,.96,saturation),smoothstep(0,.004,waterDepth));
}
MaterialSample surfaceMaterial(const Sample& s,Vec3 p,double upNormal,double snowDepth,
                               const GroundSample& ground,std::uint64_t seed,bool biomes,bool sheltered) {
    const Noise noise(seed^0x695c17ULL);
    const double patch=noise.value(p/37),grain=noise.value(p/3.7);
    const double slope=1-clamp(upNormal,0.,1.);
    double rock=smoothstep(.10,.43,slope+patch*.035);
    if(!biomes || sheltered) rock=1;
    const double dry=biomes ? 1-smoothstep(180,460,s.precipitation) : 0;
    const double sand=std::max(clamp(s.sand,0.,1.),dry)*(1-rock);
    const double snow=sheltered ? 0 : smoothstep(.002,.065,snowDepth)*smoothstep(.25,.8,upNormal);
    const double soil=std::max(0.,1-rock-sand);
    SurfaceMaterial m;
    m.weights={static_cast<float>(rock*(1-snow)),static_cast<float>(soil*(1-snow)),static_cast<float>(sand*(1-snow)),static_cast<float>(snow)};
    const double climate=biomes ? materialWetness(.2+.55*clamp(s.precipitation/1800,0.,1.),0)*.65 : 0;
    const double background=sheltered ? .08 : std::max({climate,clamp(s.wetness,0.,1.)*.8,s.ocean || s.freshwater ? 1. : 0.});
    m.backgroundWetness=static_cast<float>(background);
    m.wetness=static_cast<float>(ground.available && !sheltered ? mix(background,materialWetness(ground.saturation,ground.depth),ground.weight) : background);
    const double vegetation=biomes ? smoothstep(250,1050,s.precipitation)*(1-smoothstep(3200,4800,s.elevation)) : 0;
    const Color soilColor=blend({.43f,.32f,.21f},{.24f,.34f,.18f},vegetation);
    Color rockColor{.43f,.44f,.43f};
    if(s.landform==Landform::Canyon || s.landform==Landform::Plateau) rockColor={.60f,.38f,.25f};
    else if(s.landform==Landform::Karst) rockColor={.56f,.54f,.46f};
    const double strata=.94+.06*std::sin(s.elevation*.67+patch*2);
    rockColor={static_cast<float>(rockColor.r*strata),static_cast<float>(rockColor.g*strata),static_cast<float>(rockColor.b*strata)};
    const std::array<Color,4> colors{rockColor,soilColor,Color{.74f,.64f,.44f},Color{.87f,.92f,.96f}};
    Color color{};const double variation=1+(.10*patch+.035*grain)*(1-snow);
    for(std::size_t i=0;i<4;++i) {
        color.r+=colors[i].r*m.weights[i]*static_cast<float>(variation);
        color.g+=colors[i].g*m.weights[i]*static_cast<float>(variation);
        color.b+=colors[i].b*m.weights[i]*static_cast<float>(variation);
    }
    return {color,m};
}
Color wetMaterialColor(Color dry,const SurfaceMaterial& m) {
    const float dark=1-m.wetness*(.28f*m.weights[0]+.42f*m.weights[1]+.34f*m.weights[2]+.08f*m.weights[3]);
    return {dry.r*dark,dry.g*dark,dry.b*dark};
}
double materialRoughness(const SurfaceMaterial& m) {
    const double dry=.82*m.weights[0]+.96*m.weights[1]+.91*m.weights[2]+.78*m.weights[3];
    return mix(dry,.24+.4*m.weights[3],m.wetness);
}
Vec3 materialOrigin(Vec3 p) {
    return {std::remainder(p.x,materialPeriod),std::remainder(p.y,materialPeriod),std::remainder(p.z,materialPeriod)};
}
std::vector<MaterialMip> makeMaterialTexture(int size) {
    if(size<16 || size>1024 || (size&(size-1))) throw std::invalid_argument("Material texture size must be a power of two from 16 to 1024");
    std::vector<MaterialMip> levels;levels.push_back({size,std::vector<unsigned char>(static_cast<std::size_t>(size*size*4))});
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        const double u=(x+.5)/size,v=(y+.5)/size;
        const double broad=tileNoise(u,v,8,171),fine=tileNoise(u,v,64,291),grit=tileNoise(u,v,128,318);
        const double vein=1-smoothstep(.015,.095,std::abs(tileNoise(u,v,12,432)-.5));
        const double rock=.55+.27*(broad-.5)+.14*(fine-.5)-.20*vein;
        const double soil=.5+.30*(tileNoise(u,v,24,751)-.5)+.19*(grit-.5);
        const double ripple=std::sin(tau*(v*24+.6*std::sin(u*tau*2)));
        const double sand=.5+.065*ripple+.13*(grit-.5);
        const double snow=.5+.09*(broad-.5)+.075*(grit-.5);
        const std::array values{rock,soil,sand,snow};
        for(std::size_t c=0;c<4;++c) levels[0].rgba[static_cast<std::size_t>((y*size+x)*4)+c]=static_cast<unsigned char>(std::lround(clamp(values[c],0.,1.)*255));
    }
    while(size>1) {
        const auto& source=levels.back().rgba;const int nextSize=size/2;
        MaterialMip next{nextSize,std::vector<unsigned char>(static_cast<std::size_t>(nextSize*nextSize*4))};
        for(int y=0;y<nextSize;++y) for(int x=0;x<nextSize;++x) for(int c=0;c<4;++c) {
            const auto i=static_cast<std::size_t>((2*y*size+2*x)*4+c);
            next.rgba[static_cast<std::size_t>((y*nextSize+x)*4+c)]=static_cast<unsigned char>((source[i]+source[i+4]+source[i+static_cast<std::size_t>(size*4)]+source[i+static_cast<std::size_t>(size*4+4)]+2)/4);
        }
        levels.push_back(std::move(next));size=nextSize;
    }
    return levels;
}
} // namespace planet

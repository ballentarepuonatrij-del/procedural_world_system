#include "planet/surface_detail.hpp"
#include <stdexcept>

namespace planet {
double SurfaceObject::collisionRadius() const {
    if(kind==DetailKind::Tree) return height*0.025;
    return kind==DetailKind::Rock ? radius*0.8 : 0;
}
double SurfaceObject::collisionHeight() const {
    return kind==DetailKind::Tree ? height*0.65 : height;
}
std::vector<SurfaceObject> surfaceObjects(const World& world,Vec3 center,double radius,bool groundCover) {
    if(!std::isfinite(radius) || radius<=0 || radius>600 ||
       !std::isfinite(length(center)) || length(center)<0.5)
        throw std::invalid_argument("Surface detail requires a finite direction and radius in (0,600] m");
    std::vector<SurfaceObject> result;
    if(!world.modules().enabled(Module::SurfaceDetail)) return result;
    center=normalized(center);
    const double planetRadius=world.config().radius;
    const auto seed=world.config().seed;
    const Noise patches(seed^0x7ace1359ULL);
    constexpr std::array<Vec3,6> faces{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    for(unsigned layer=0;layer<(groundCover ? 2U : 1U);++layer) {
        const double reach=layer==0 ? radius : std::min(radius,80.0);
        const double cell=layer==0 ? 24.0 : 5.0;
        for(std::size_t faceIndex=0;faceIndex<faces.size();++faceIndex) {
            const Vec3 face=faces[faceIndex];
            const double facing=dot(center,face);
            if(facing<0.5) continue;
            const Vec3 reference=std::abs(face.y)<0.5 ? Vec3{0,1,0} : Vec3{0,0,1};
            const Vec3 right=normalized(cross(reference,face)), down=cross(right,face);
            const double cx=dot(center,right)*planetRadius/facing, cy=dot(center,down)*planetRadius/facing;
            // In a cube's own face chart the gnomonic projection expands
            // distances by at most 3; the margin covers curvature and cell jitter.
            const double bound=reach*3.1+cell;
            const auto firstX=static_cast<std::int64_t>(std::floor(std::max(-planetRadius,cx-bound)/cell));
            const auto lastX=static_cast<std::int64_t>(std::floor(std::min(planetRadius,cx+bound)/cell));
            const auto firstY=static_cast<std::int64_t>(std::floor(std::max(-planetRadius,cy-bound)/cell));
            const auto lastY=static_cast<std::int64_t>(std::floor(std::min(planetRadius,cy+bound)/cell));
            for(auto y=firstY;y<=lastY;++y) for(auto x=firstX;x<=lastX;++x) {
                auto id=hash64(seed^hash64(faceIndex+layer*6));
                id=hash64(id^static_cast<std::uint64_t>(x));
                id=hash64(id^static_cast<std::uint64_t>(y));
                const double px=(static_cast<double>(x)+0.18+0.64*randomUnit(id))*cell;
                const double py=(static_cast<double>(y)+0.18+0.64*randomUnit(id+1))*cell;
                if(px < -planetRadius || px>=planetRadius || py < -planetRadius || py>=planetRadius) continue;
                const Vec3 p=normalized(face*planetRadius+right*px+down*py);
                if(length(p-center)*planetRadius>reach) continue;
                // Correct the cube projection's area distortion statistically.
                const double area=std::pow(dot(p,face),3);
                if(randomUnit(id+2)>area) continue;
                const auto habitat=world.climatology(p);
                if(habitat.ocean) continue;
                if(const auto site=world.caveSite(p); site && site->protectedGround(p)) continue;
                const double h=habitat.elevation;
                const double dx=(world.elevation(normalized(p+east(p)*(2/planetRadius)))-h)/2;
                const double dy=(world.elevation(normalized(p+north(p)*(2/planetRadius)))-h)/2;
                const double slope=std::hypot(dx,dy);
                const bool plants=world.modules().enabled(Module::Biomes) &&
                    habitat.temperature>267 && habitat.temperature<315 &&
                    h>world.config().seaLevel+2 && habitat.precipitation>180 && slope<0.65;
                const double patch=clamp(0.55+0.65*patches.value(p*(planetRadius/95)),0.08,1.0);
                const double choice=randomUnit(id+3);
                SurfaceObject object{id,DetailKind::Rock,p,h,0,0,tau*randomUnit(id+4),false};
                if(layer==1) {
                    if(!plants || choice>patch*0.85) continue;
                    object.kind=habitat.biome==Biome::Desert || choice<0.06 ? DetailKind::Shrub : DetailKind::Grass;
                    object.height=object.kind==DetailKind::Shrub ? 0.35+0.65*randomUnit(id+5) : 0.18+0.40*randomUnit(id+5);
                    object.radius=object.height*0.65;
                } else {
                    const bool woods=habitat.biome==Biome::Forest || habitat.biome==Biome::Rainforest;
                    const double trees=woods ? 0.82 : (habitat.biome==Biome::Grassland ? 0.12 : 0.0);
                    if(plants && habitat.temperature>277 && choice<trees*patch) {
                        object.kind=DetailKind::Tree;
                        object.height=(habitat.biome==Biome::Rainforest ? 10 : 6)+9*randomUnit(id+5);
                        object.radius=object.height*(0.22+0.10*randomUnit(id+6));
                        object.evergreen=habitat.temperature<285;
                    } else if(choice>0.76 && slope<1.2 && habitat.biome!=Biome::Ice) {
                        object.height=0.35+2.3*std::pow(randomUnit(id+5),2);
                        object.radius=object.height*(0.65+0.45*randomUnit(id+6));
                    } else if(plants && choice<0.72 && choice>0.60) {
                        object.kind=DetailKind::Shrub;
                        object.height=0.7+0.8*randomUnit(id+5); object.radius=object.height*0.75;
                    } else continue;
                }
                result.push_back(object);
            }
        }
    }
    return result;
}
} // namespace planet

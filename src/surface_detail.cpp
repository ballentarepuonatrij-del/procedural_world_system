#include "planet/surface_detail.hpp"
#include "planet/rivers.hpp"
#include <stdexcept>

namespace planet {
Landmark findMeadow(const World& world) {
    if(!world.modules().enabled(Module::Biomes) || !world.modules().enabled(Module::SurfaceDetail))
        throw std::runtime_error("Enable Biomes and Surface detail to visit a meadow");
    const auto home=world.findLand();std::vector<std::pair<double,Vec3>> candidates;
    for(int i=0;i<1024;++i) {
        const auto p=direction(std::asin(1-2*(i+.5)/1024),i*pi*(3-std::sqrt(5.0))+randomUnit(world.config().seed)*tau);
        const auto s=world.climatology(p);
        if(s.ocean || s.elevation<world.config().seaLevel+10 || s.temperature<282 || s.temperature>296 ||
           s.precipitation<600 || s.precipitation>1800 || std::abs(latitude(p))<radians(20)) continue;
        candidates.emplace_back(length(p-home)+std::abs(s.temperature-290)*.16+(s.biome==Biome::Grassland ? 0 : .7),p);
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    for(std::size_t i=0;i<std::min<std::size_t>(24,candidates.size());++i) {
        const auto p=candidates[i].second;const auto sites=surfaceSites(world,p,55);
        std::array<bool,speciesCount> kinds{};const SurfaceObject* target=nullptr;double best=1e30;
        for(const auto& site:sites) if(site.species>=Species::Daisy && site.species<=Species::Buttercup && site.flowerCycle.enabled) {
            kinds[static_cast<unsigned>(site.species)]=true;
            const double distance=length(site.up-p)*world.config().radius;
            const auto plant=grownObject(site);const auto& flower=flowerTraits(site.species);
            // A mature, low flower head is easier to inspect from the path.
            // The route stays fixed through the seasons; it does not force bloom.
            const double score=distance+(site.species==Species::Fireweed ? 60 : 0);
            if(plant.height>.3 && plant.plant.stage==GrowthStage::Mature &&
               plant.plant.ageDays>flower.reproductiveDays+40 && score<best) {target=&site;best=score;}
        }
        if(!target || std::count(kinds.begin(),kinds.end(),true)<4) continue;
        const auto start=normalized(target->up-north(target->up)*(2/world.config().radius));
        if(std::abs(world.elevation(start)-target->elevation)>1 || world.surface(start)>world.elevation(start)+.1) continue;
        return {start,target->up,world.relief(start).type};
    }
    throw std::runtime_error("No suitable flowering meadow on this planet");
}
Landmark findGrove(const World& world) {
    const auto home=world.findLand();
    if(!world.modules().enabled(Module::Biomes) || !world.modules().enabled(Module::SurfaceDetail)) return {home,home,Landform::Lowland};
    std::vector<std::pair<double,Vec3>> candidates;
    for(int i=0;i<768;++i) {
        const auto p=direction(std::asin(1-2*(i+.5)/768),i*pi*(3-std::sqrt(5.0))+randomUnit(world.config().seed)*tau);
        const auto s=world.climatology(p);
        if(s.ocean || (s.biome!=Biome::Forest && s.biome!=Biome::Rainforest) || s.temperature<279 || s.temperature>305) continue;
        const double score=length(p-home)+std::abs(s.temperature-289)*.10;
        candidates.emplace_back(score,p);
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    for(const auto& candidate:candidates) {
        const auto sites=surfaceSites(world,candidate.second,100,false);
        if(std::count_if(sites.begin(),sites.end(),[](const auto& o){return o.kind==DetailKind::Tree;})<8) continue;
        for(const auto& tree:sites) if(tree.kind==DetailKind::Tree && grownObject(tree).height>6) {
            const auto start=normalized(tree.up-north(tree.up)*(25/world.config().radius));
            if(std::abs(world.elevation(start)-tree.elevation)<6 && world.elevation(start)>world.config().seaLevel+2)
                return {start,tree.up,world.relief(start).type};
        }
    }
    return {home,home,Landform::Lowland};
}
double SurfaceObject::collisionRadius() const {
    if(kind==DetailKind::Tree) return height*0.025;
    return kind==DetailKind::Rock ? radius*0.8 : 0;
}
double SurfaceObject::collisionHeight() const {
    return kind==DetailKind::Tree ? height*0.65 : height;
}
SurfaceObject grownObject(SurfaceObject object,const Flora* flora) {
    if(object.species==Species::None) return object;
    object.plant=plantState(object.species,object.id,flora ? flora->days() : 0,object.suitability,flora && flora->young());
    object.flowers=flowerState(object.species,object.plant,object.flowerCycle,flora ? flora->calendarDay() : 79);
    object.height*=object.plant.size; object.radius*=object.plant.size;
    return object;
}
std::vector<SurfaceObject> surfaceObjects(const World& world,Vec3 center,double radius,bool groundCover,const Flora* flora) {
    auto sites=surfaceSites(world,center,radius,groundCover);
    for(auto& site:sites) site=grownObject(site,flora);
    std::erase_if(sites,[](const auto& o) { return o.height<.015; });
    return sites;
}
std::vector<SurfaceObject> surfaceSites(const World& world,Vec3 center,double radius,bool groundCover) {
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
                if(world.modules().enabled(Module::Rivers) && world.waterNetwork()->sample(p).wet) continue;
                const double depth=habitat.ocean ? world.config().seaLevel-habitat.elevation : habitat.waterDepth;
                if(habitat.ocean && (depth<.5 || depth>30)) continue;
                if(const auto site=world.caveSite(p); site && site->protectedGround(p)) continue;
                const double h=habitat.elevation;
                const double dx=(world.elevation(normalized(p+east(p)*(2/planetRadius)))-h)/2;
                const double dy=(world.elevation(normalized(p+north(p)*(2/planetRadius)))-h)/2;
                const double slope=std::hypot(dx,dy);
                const bool plants=world.modules().enabled(Module::Biomes) &&
                    habitat.temperature>267 && habitat.temperature<315 &&
                    h>world.config().seaLevel+2 && habitat.precipitation>180 && slope<0.65;
                const double patch=clamp(0.55+0.65*patches.value(p*(planetRadius/95))+.25*habitat.wetness,0.08,1.0);
                const double choice=randomUnit(id+3);
                SurfaceObject object{id,DetailKind::Rock,p,h,0,0,tau*randomUnit(id+4),false,Species::None,0,{},{},{}};
                object.waterDepth=depth;
                if(habitat.ocean || (habitat.freshwater && (layer==1 || depth>.35))) {
                    if(layer==0 || !world.modules().enabled(Module::Biomes) || choice>patch*.85) continue;
                    object.kind=DetailKind::Grass;
                } else if(layer==1) {
                    if(!plants || choice>patch*0.85) continue;
                    object.kind=habitat.biome==Biome::Desert || choice<0.06 ? DetailKind::Shrub : DetailKind::Grass;
                    object.height=object.kind==DetailKind::Shrub ? 0.35+0.65*randomUnit(id+5) : 0.18+0.40*randomUnit(id+5);
                    object.radius=object.height*0.65;
                } else {
                    const bool woods=habitat.biome==Biome::Forest || habitat.biome==Biome::Rainforest;
                    const double trees=woods ? 0.82 : habitat.wetness>.4 ? .42 : (habitat.biome==Biome::Grassland ? 0.12 : 0.0);
                    if(plants && choice<trees*patch) {
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
                if(object.kind!=DetailKind::Rock) {
                    const double rockiness=clamp(.5+.6*patches.value(p*(planetRadius/47)+Vec3{3,7,2}),0.0,1.0);
                    std::array<double,speciesCount> weights{};
                    double total=0;
                    for(unsigned i=1;i<speciesCount;++i) {
                        const auto species=static_cast<Species>(i);
                        const auto& traits=plantTraits(species);
                        const bool tree=traits.woody && traits.height>5;
                        const bool shrub=species==Species::Juniper || species==Species::Heather;
                        const bool matches=habitat.ocean ? traits.aquatic :
                            (object.kind==DetailKind::Tree ? tree : (object.kind==DetailKind::Shrub ? shrub : (!tree && !shrub && (!traits.aquatic || freshwaterPlant(species)))));
                        if(matches) weights[i]=plantSuitability(species,habitat,slope,rockiness,world.config().seaLevel);
                        total+=weights[i];
                    }
                    if(total<=0) continue;
                    double pick=randomUnit(id+149)*total;
                    for(unsigned i=1;i<speciesCount;++i) if(weights[i]>0) {
                        pick-=weights[i];
                        object.species=static_cast<Species>(i);object.suitability=weights[i];
                        if(pick<=0) break;
                    }
                    const auto& traits=plantTraits(object.species);
                    object.height=traits.height*(.72+.40*randomUnit(id+5));
                    if((traits.aquatic && !freshwaterPlant(object.species)) || object.species==Species::Pondweed) object.height=std::min(object.height,depth*.85);
                    object.radius=object.height*traits.widthRatio;
                    object.evergreen=object.species==Species::Pine;
                    object.flowerCycle=flowerCycle(object.species,id,world.config(),p,habitat);
                }
                result.push_back(object);
            }
        }
    }
    return result;
}
} // namespace planet

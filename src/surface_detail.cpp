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
        if(s.ocean || !forestBiome(s.biome) || s.temperature<279 || s.temperature>305) continue;
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
Landmark findForest(const World& world,Biome biome,double seconds) {
    if(!forestBiome(biome) || !world.modules().enabled(Module::Biomes) || !world.modules().enabled(Module::SurfaceDetail))
        throw std::invalid_argument("Enable Biomes and Surface detail to visit a forest");
    const auto home=world.findLand();std::vector<std::pair<double,Vec3>> candidates;
    for(int i=0;i<3072;++i) {
        const auto p=direction(std::asin(1-2*(i+.5)/3072),i*pi*(3-std::sqrt(5.)));
        const auto s=world.climatology(p);if(s.biome!=biome || s.elevation<world.config().seaLevel+20 || s.elevation>2500) continue;
        const double slope=std::abs(world.elevation(normalized(p+east(p)*(50/world.config().radius)))-s.elevation)/50;
        if(slope>.3) continue;
        const double seasonal=world.sample(p,seconds).temperature;
        const double temperateLatitude=biome==Biome::TemperateRainforest ? std::max(0.,30-std::abs(degrees(latitude(p))))*.2 : 0;
        candidates.emplace_back(length(p-home)+slope*4+.15*std::abs(seasonal-292)+temperateLatitude,p);
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    // Prefer a giant canopy, but a seed with younger/sparser tropical islands
    // must still offer a route into its actual forest habitat.
    for(int pass=0;pass<2;++pass) for(std::size_t i=0;i<std::min<std::size_t>(32,candidates.size());++i) {
        const auto trees=surfaceObjects(world,candidates[i].second,100,false);const SurfaceObject* target=nullptr;
        for(const auto& o:trees) if(o.kind==DetailKind::Tree && (!target || o.height>target->height)) target=&o;
        if(!target || target->height<(pass==0 ? biome==Biome::Rainforest ? 45 : biome==Biome::TemperateRainforest ? 50 : 25 : 25)) continue;
        for(int ray=0;ray<24;++ray) {
            const auto axis=east(target->up)*std::cos(tau*ray/24)+north(target->up)*std::sin(tau*ray/24);
            const double range=std::max(25.,target->height*.65);
            const auto p=normalized(target->up+axis*(range/world.config().radius));
            const auto h=world.climatology(p);if(h.biome!=biome || h.freshwater || std::abs(h.elevation-target->elevation)>range*.3) continue;
            bool clear=true;for(const auto& tree:trees) if(length(tree.up-p)*world.config().radius<tree.collisionRadius()+1) {clear=false;break;}
            if(clear && !world.caveSite(p)) return {p,target->up,h.landform};
        }
    }
    throw std::runtime_error("No accessible mature forest of this type was found");
}
double SurfaceObject::collisionRadius() const {
    if(kind==DetailKind::Tree) return height*(species==Species::Redwood || species==Species::Kapok || species==Species::StranglerFig ? .033 : species==Species::Palm ? .014 : .025);
    return kind==DetailKind::Rock ? radius*0.8 : 0;
}
double SurfaceObject::collisionHeight() const {
    return kind==DetailKind::Tree ? height*0.65 : height;
}
unsigned SurfaceObject::lianas() const {
    if(!tropicalCanopy(species) || height<12 || plant.stage==GrowthStage::Dead) return 0;
    return species==Species::StranglerFig ? 5U : randomUnit(id+1901)<.75 ? 2U+static_cast<unsigned>(3*randomUnit(id+1902)) : 0U;
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
    for(unsigned layer=0;layer<3;++layer) {
        if((layer==1 && !groundCover) || (layer==2 && !world.modules().enabled(Module::Biomes))) continue;
        const double reach=layer==1 ? std::min(radius,80.0) : radius;
        const double cell=layer==0 ? 24.0 : layer==1 ? 5.0 : 16.0;
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
                if(layer==2 && !forestBiome(habitat.biome)) continue;
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
                const bool woods=forestBiome(habitat.biome);
                const double patch=clamp((woods ? .85 : .55)+.65*patches.value(p*(planetRadius/95))+.25*habitat.wetness,woods ? .60 : .08,1.0);
                const double choice=randomUnit(id+3);
                SurfaceObject object{id,DetailKind::Rock,p,h,0,0,tau*randomUnit(id+4),false,Species::None,0,{},{},{}};
                object.waterDepth=depth;
                if(habitat.ocean || (habitat.freshwater && (layer==1 || depth>.35))) {
                    if(layer==0 || !world.modules().enabled(Module::Biomes) || choice>patch*.85) continue;
                    object.kind=DetailKind::Grass;
                } else if(layer==2) {
                    if(!plants || choice>patch*.90) continue;
                    object.kind=DetailKind::Tree;
                } else if(layer==1) {
                    if(!plants || choice>patch*0.85) continue;
                    object.kind=habitat.biome==Biome::Desert || choice<0.06 ? DetailKind::Shrub : DetailKind::Grass;
                    object.height=object.kind==DetailKind::Shrub ? 0.35+0.65*randomUnit(id+5) : 0.18+0.40*randomUnit(id+5);
                    object.radius=object.height*0.65;
                } else {
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
                        const bool tree=treeSpecies(species);
                        const bool shrub=species==Species::Juniper || species==Species::Heather;
                        const bool matches=habitat.ocean ? traits.aquatic :
                            (object.kind==DetailKind::Tree ? tree : (object.kind==DetailKind::Shrub ? shrub : (!tree && !shrub && (!traits.aquatic || freshwaterPlant(species)))));
                        if(matches && !(layer==2 && (species==Species::Redwood || species==Species::Kapok)))
                            weights[i]=plantSuitability(species,habitat,slope,rockiness,world.config().seaLevel);
                        if(woods && layer==1 && species>=Species::Daisy && species<=Species::Buttercup) weights[i]*=.15;
                        if(habitat.biome==Biome::Rainforest && (species==Species::Oak || species==Species::Acacia || species==Species::Juniper ||
                            species==Species::Beech || species==Species::MeadowGrass)) weights[i]*=.12;
                        total+=weights[i];
                    }
                    if(total<=0) continue;
                    double pick=randomUnit(id+149)*total;
                    for(unsigned i=1;i<speciesCount;++i) if(weights[i]>0) {
                        pick-=weights[i];
                        object.species=static_cast<Species>(i);
                        if(pick<=0) break;
                    }
                    // Relative abundance is separate from an individual's growth potential.
                    object.suitability=plantSuitability(object.species,habitat,slope,rockiness,world.config().seaLevel);
                    const auto& traits=plantTraits(object.species);
                    object.height=traits.height*(.72+.40*randomUnit(id+5));
                    if((traits.aquatic && !freshwaterPlant(object.species)) || object.species==Species::Pondweed) object.height=std::min(object.height,depth*.85);
                    object.radius=object.height*traits.widthRatio;
                    object.evergreen=coniferSpecies(object.species);
                    object.flowerCycle=flowerCycle(object.species,id,world.config(),p,habitat);
                }
                result.push_back(object);
            }
        }
    }
    return result;
}
} // namespace planet

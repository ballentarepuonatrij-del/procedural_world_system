#pragma once
#include "planet/hydrology.hpp"
#include "planet/shadows.hpp"
#include "planet/materials.hpp"

namespace planet {
// CPU-only scene data. A worker owns copies of World/Hydrology; OpenGL upload
// and adoption of this complete snapshot stay on the window thread.
struct TerrainVertex {Vec3 position,normal;Color color;double u,v;SurfaceMaterial material{};};
struct GridVertex {Vec3 position;float alpha;};
struct SurfaceWaterVertex {Vec3 up,position;double altitude;bool available,river;double spacing;bool inland=false,wetland=false;double level=0;};
struct PlantSite {SurfaceObject object;Vec3 base;double snowDepth=0;};
struct PlantMesh {
    std::vector<TerrainVertex> vertices;
    std::vector<Triangle> casters;
    std::optional<SurfaceObject> focus;
    std::size_t objects=0,plants=0,marine=0,freshwater=0,species=0,flowering=0,budding=0,seedHeads=0,trees=0,lianaHosts=0;
    double heightSum=0,maximumHeight=0;
};
PlantMesh makePlantMesh(std::span<const PlantSite> sites,Vec3 anchor,Vec3 east,Vec3 north,double radius,const Flora& flora);
struct TerrainMesh {
    Vec3 anchor,east,north;
    double spacing=2,eyeHeight=0,seconds=0;
    std::uint64_t waterRevision=0,waterSteps=0,snowRevision=0;
    std::vector<TerrainVertex> terrain,caves;
    std::vector<GridVertex> grid;
    std::vector<Triangle> casters;
    std::vector<SurfaceWaterVertex> waterVertices;
    std::vector<std::array<std::size_t,3>> waterTriangles;
    std::vector<PlantSite> plants;
    std::vector<std::pair<std::size_t,std::size_t>> rivers;
};
double terrainSpacing(const World& world,Vec3 observer,double altitude,double minimumStep=2);
TerrainMesh makeTerrainMesh(const World& world,const Hydrology& water,Vec3 observer,double altitude,double seconds,double minimumStep=2);
} // namespace planet

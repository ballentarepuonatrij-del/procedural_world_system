#pragma once
#include "planet/hydrology.hpp"
#include "planet/shadows.hpp"

namespace planet {
// CPU-only scene data. A worker owns copies of World/Hydrology; OpenGL upload
// and adoption of this complete snapshot stay on the window thread.
struct TerrainVertex {Vec3 position,normal;Color color;double u,v;};
struct GridVertex {Vec3 position;float alpha;};
struct SurfaceWaterVertex {Vec3 up,position;double altitude;bool available,river;double spacing;bool inland=false,wetland=false;double level=0;};
struct PlantSite {SurfaceObject object;Vec3 base;};
struct TerrainMesh {
    Vec3 anchor,east,north;
    double spacing=2,eyeHeight=0,seconds=0;
    std::uint64_t waterRevision=0,waterSteps=0;
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

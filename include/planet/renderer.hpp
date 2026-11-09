#pragma once
#include "planet/navigation.hpp"
#include "planet/hydrology.hpp"
#include "planet/globe_camera.hpp"
#include "planet/lighting.hpp"
#include "planet/shadows.hpp"
#include "planet/rivers.hpp"
#include "planet/terrain_mesh.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <filesystem>
#include <string>
#include <future>

namespace planet {
enum class SurfaceStyle { Natural, Grid, Wireframe };
enum class WeatherLayer { Clouds, Pressure, Wind, Rivers, Currents, Off };
inline const char* weatherLayerName(WeatherLayer layer) {
    switch(layer) { case WeatherLayer::Clouds:return "clouds";case WeatherLayer::Pressure:return "pressure";case WeatherLayer::Wind:return "wind";case WeatherLayer::Rivers:return "rivers";case WeatherLayer::Currents:return "currents";default:return "off"; }
}
inline const char* surfaceStyleName(SurfaceStyle style) {
    switch(style) {
        case SurfaceStyle::Grid: return "grid";
        case SurfaceStyle::Wireframe: return "wireframe";
        default: return "natural";
    }
}
struct ViewState {
    int width=1280, height=800;
    double seconds=79*86400.0, timeScale=1, fps=0;
    double frameSeconds=0;
    bool globe=false, help=true, captured=false, paused=false;
    // Flow markers are opt-in diagnostics; ordinary exploration starts clean.
    bool windTracers=false,waterTracers=false,headlamp=true;
    bool floraInfo=true,faunaInfo=false;
    SurfaceStyle surfaceStyle=SurfaceStyle::Natural;
    WeatherLayer weatherLayer=WeatherLayer::Clouds;
    double volume=.55;
    bool audioAvailable=false;
    bool interfaceVisible=true;
    bool skyEnabled=true,shadowsEnabled=true;
    GlobeCamera globeCamera;
};
bool globeHudHit(const ViewState& view,int x,int y);
struct RenderTimings {
    double terrain=0,plants=0,shadowBuild=0,shadows=0,water=0,rivers=0,weather=0,sky=0,fauna=0,snow=0;
};
class Renderer {
public:
    explicit Renderer(HDC dc);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void invalidate() { dirty_=true;globeDirty_=true;weatherDirty_=true;waterMapDirty_=true;++terrainEpoch_; }
    void draw(const World& world, const Explorer& explorer, const Simulation& simulation,const ViewState& view);
    void capture(const std::filesystem::path& path, int width, int height) const;
    std::string device() const;
    std::size_t detailCount() const { return detailCount_; }
    std::size_t cloudCount() const { return cloudCount_; }
    std::size_t waterCount() const { return waterCount_; }
    std::size_t caveCount() const { return caveCount_; }
    std::size_t plantCount() const { return plantCount_; }
    std::size_t marineCount() const { return marineCount_; }
    std::size_t freshwaterCount() const {return freshwaterCount_;}
    std::size_t plantSpeciesCount() const { return plantSpeciesCount_; }
    double plantHeightSum() const { return plantHeightSum_; }
    std::size_t floweringCount() const { return floweringCount_; }
    std::size_t buddingCount() const { return buddingCount_; }
    std::size_t seedHeadCount() const { return seedHeadCount_; }
    std::size_t rainCount() const { return rainCount_; }
    std::size_t lightningCount() const { return lightningCount_; }
    std::size_t stormMarkerCount() const { return stormMarkerCount_; }
    std::size_t starCount() const { return starCount_; }
    std::size_t shadowCount() const { return shadowCount_; }
    bool shadowSupport() const { return stencilBits_>=8; }
    bool shadowsUsed() const { return shadowsUsed_; }
    const Lighting& illumination() const { return lighting_; }
    std::size_t riverTriangles() const { return riverTriangles_; }
    const RenderTimings& timings() const { return timings_; }
    std::uint64_t terrainJobs() const {return terrainJobs_;}
    std::uint64_t terrainUploads() const {return terrainUploads_;}
    std::uint64_t terrainDiscarded() const {return terrainDiscarded_;}
    double surfaceSpacing() const {return terrainStep_;}
    std::size_t deerCount() const {return deerCount_;}
    std::size_t snowCount() const {return snowCount_;}
    std::size_t coarseSnowCount() const {return coarseSnowCount_;}
    std::size_t blowingSnowCount() const {return blowingSnowCount_;}
private:
    RenderTimings timings_;
    void buildTerrain(const World& world,const Explorer& explorer,const Hydrology& water,double seconds);
    void uploadTerrain(TerrainMesh mesh);
    void adoptTerrain(TerrainMesh mesh);
    struct SceneMesh {TerrainMesh terrain;Flora flora;Vec3 sun;std::vector<Triangle> shadows;bool hasShadows=false;};
    struct MeshUpload {SceneMesh scene;GLuint lists=0;std::array<std::size_t,4> chunks{};std::size_t next=0;std::uint64_t epoch=0;};
    void beginUpload(SceneMesh scene);
    void advanceUpload(const Flora& flora,Vec3 sunlight);
    void discardUpload();
    void buildGlobe(const World& world,double seconds);
    void buildObjects(const World& world,const Flora& flora);
    void drawSky(const World& world,const Explorer& explorer,const ViewState& view);
    void drawShadows(Vec3 sun,bool textured);
    void prepareFauna(const World& world,const Explorer& explorer,const Fauna& fauna,bool shadows,Vec3 sun);
    void emitFauna();
    void drawWeather(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight);
    void drawSnow(const World& world,const Explorer& explorer,const Simulation& simulation,double daylight);
    void drawGlobalWeather(const GlobalWeather& weather,const ViewState& view);
    void drawWater(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight);
    void drawRivers(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight);
    void drawGlobalWater(const World& world,const ViewState& view);
    void drawHud(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view);
    void text(float x,float y,const std::string& value,Color color,bool large=false);
    GLuint terrain_=0, terrainGrid_=0, globe_=0, font_=0, headingFont_=0, detailTexture_=0;
    GLuint objects_=0,caves_=0;
    std::size_t caveCount_=0;
    std::size_t detailCount_=0;
    std::vector<PlantSite> objectSites_;
    std::optional<SurfaceObject> focusPlant_;
    std::size_t plantCount_=0,marineCount_=0,freshwaterCount_=0,plantSpeciesCount_=0;
    std::size_t floweringCount_=0,buddingCount_=0,seedHeadCount_=0;
    double plantHeightSum_=0,lastFloraDay_=-1e30,lastFloraCalendar_=-1e30;
    bool lastYoung_=false;
    GLuint cloudTexture_=0,snowTexture_=0;
    std::size_t cloudCount_=0;
    std::vector<SurfaceWaterVertex> waterVertices_;
    std::vector<std::array<std::size_t,3>> waterTriangles_;
    struct WaterDrawVertex { Vec3 position; Color color; float alpha; double depth; };
    std::vector<WaterDrawVertex> waterFrame_,waterDraw_;
    std::vector<GroundSample> waterGround_;
    std::vector<unsigned char> waterVisible_;
    std::size_t waterCount_=0;
    std::uint64_t waterRevision_=0,waterSteps_=0,snowRevision_=0;
    Vec3 anchor_{1,0,0}, east_{0,0,1}, north_{0,1,0};
    double lastTerrainTime_=-1e30, lastGlobeTime_=-1e30;
    double terrainStep_=2, terrainEyeHeight_=0;
    bool dirty_=true, globeDirty_=true;
    std::future<SceneMesh> terrainJob_;
    std::optional<MeshUpload> meshUpload_;
    GLuint terrainChunks_=0;
    GLsizei terrainChunksCount_=0;
    std::optional<Flora> preparedFlora_;
    bool preparedShadow_=false;
    std::uint64_t terrainEpoch_=0,terrainJobEpoch_=0;
    std::uint64_t terrainJobs_=0,terrainUploads_=0,terrainDiscarded_=0;
    Vec3 previousObserver_{};
    double flightDetailStep_=2;
    GLuint weatherMap_=0;
    std::uint64_t lastGlobalStep_=0;
    WeatherLayer lastWeatherLayer_=WeatherLayer::Off;
    bool weatherDirty_=true,lastGlobalClouds_=true;
    std::size_t rainCount_=0,lightningCount_=0,stormMarkerCount_=0;
    Lighting lighting_;
    std::vector<Star> stars_;
    std::uint64_t starSeed_=0;
    std::vector<Triangle> terrainCasters_,objectCasters_;
    std::vector<MeshFace> deerFaces_;
    std::vector<Triangle> deerShadows_;
    std::size_t deerCount_=0;
    std::size_t snowCount_=0,coarseSnowCount_=0,blowingSnowCount_=0;
    GLuint shadows_=0;
    Vec3 shadowSun_{};
    bool shadowDirty_=true,shadowsUsed_=false,stencilWrap_=false;
    GLint stencilBits_=0;
    std::size_t starCount_=0,shadowCount_=0;
    std::vector<std::pair<std::size_t,std::size_t>> riverSegments_;
    std::size_t riverTriangles_=0;
    GLuint waterMap_=0,riverTexture_=0;
    bool waterMapDirty_=true;
    WeatherLayer waterMapLayer_=WeatherLayer::Off;
};
} // namespace planet

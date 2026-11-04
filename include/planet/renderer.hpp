#pragma once
#include "planet/navigation.hpp"
#include "planet/hydrology.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <filesystem>
#include <string>

namespace planet {
enum class SurfaceStyle { Natural, Grid, Wireframe };
enum class WeatherLayer { Clouds, Pressure, Wind, Off };
inline const char* weatherLayerName(WeatherLayer layer) {
    switch(layer) { case WeatherLayer::Clouds:return "clouds";case WeatherLayer::Pressure:return "pressure";case WeatherLayer::Wind:return "wind";default:return "off"; }
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
    bool globe=false, help=true, captured=false, paused=false;
    bool windTracers=false,waterTracers=true,headlamp=true;
    bool floraInfo=true;
    SurfaceStyle surfaceStyle=SurfaceStyle::Grid;
    WeatherLayer weatherLayer=WeatherLayer::Clouds;
    double volume=.55;
    bool audioAvailable=false;
};
class Renderer {
public:
    explicit Renderer(HDC dc);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void invalidate() { dirty_=true; globeDirty_=true; weatherDirty_=true; }
    void draw(const World& world, const Explorer& explorer, const Simulation& simulation,const ViewState& view);
    void capture(const std::filesystem::path& path, int width, int height) const;
    std::string device() const;
    std::size_t detailCount() const { return detailCount_; }
    std::size_t cloudCount() const { return cloudCount_; }
    std::size_t waterCount() const { return waterCount_; }
    std::size_t caveCount() const { return caveCount_; }
    std::size_t plantCount() const { return plantCount_; }
    std::size_t marineCount() const { return marineCount_; }
    std::size_t plantSpeciesCount() const { return plantSpeciesCount_; }
    double plantHeightSum() const { return plantHeightSum_; }
    std::size_t rainCount() const { return rainCount_; }
    std::size_t lightningCount() const { return lightningCount_; }
    std::size_t stormMarkerCount() const { return stormMarkerCount_; }
private:
    void buildTerrain(const World& world,const Explorer& explorer,const Hydrology& water,double seconds);
    void buildGlobe(const World& world,double seconds);
    void buildObjects(const World& world,const Flora& flora);
    void drawWeather(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight);
    void drawGlobalWeather(const GlobalWeather& weather,const ViewState& view);
    void drawWater(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight);
    void drawHud(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view);
    void text(float x,float y,const std::string& value,Color color,bool large=false);
    GLuint terrain_=0, terrainGrid_=0, globe_=0, font_=0, headingFont_=0, detailTexture_=0;
    GLuint objects_=0,caves_=0;
    std::size_t caveCount_=0;
    std::size_t detailCount_=0;
    struct PlantSite { SurfaceObject object; Vec3 base; };
    std::vector<PlantSite> objectSites_;
    std::optional<SurfaceObject> focusPlant_;
    std::size_t plantCount_=0,marineCount_=0,plantSpeciesCount_=0;
    double plantHeightSum_=0,lastFloraDay_=-1e30;
    bool lastYoung_=false;
    GLuint cloudTexture_=0;
    std::size_t cloudCount_=0;
    struct WaterVertex { Vec3 up,position; double altitude; bool available; double spacing; };
    std::vector<WaterVertex> waterVertices_;
    std::vector<std::array<std::size_t,3>> waterTriangles_;
    std::size_t waterCount_=0;
    std::uint64_t waterRevision_=0,waterSteps_=0;
    Vec3 anchor_{1,0,0}, east_{0,0,1}, north_{0,1,0};
    double lastTerrainTime_=-1e30, lastGlobeTime_=-1e30;
    double terrainStep_=2, terrainEyeHeight_=0;
    bool dirty_=true, globeDirty_=true;
    GLuint weatherMap_=0;
    std::uint64_t lastGlobalStep_=0;
    WeatherLayer lastWeatherLayer_=WeatherLayer::Off;
    bool weatherDirty_=true,lastGlobalClouds_=true;
    std::size_t rainCount_=0,lightningCount_=0,stormMarkerCount_=0;
};
} // namespace planet

#pragma once
#include "planet/navigation.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <filesystem>
#include <string>

namespace planet {
struct ViewState {
    int width=1280, height=800;
    double seconds=79*86400.0, timeScale=60, fps=0;
    bool globe=false, help=true, captured=false, paused=false;
};
class Renderer {
public:
    explicit Renderer(HDC dc);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void invalidate() { dirty_=true; globeDirty_=true; }
    void draw(const World& world, const Explorer& explorer, const ViewState& view);
    void capture(const std::filesystem::path& path, int width, int height) const;
    std::string device() const;
private:
    void buildTerrain(const World& world,const Explorer& explorer,double seconds);
    void buildGlobe(const World& world,double seconds);
    void drawHud(const World& world,const Explorer& explorer,const ViewState& view);
    void text(float x,float y,const std::string& value,Color color,bool large=false);
    GLuint terrain_=0, globe_=0, font_=0, headingFont_=0, detailTexture_=0;
    Vec3 anchor_{1,0,0}, east_{0,0,1}, north_{0,1,0};
    double lastTerrainTime_=-1e30, lastGlobeTime_=-1e30;
    bool dirty_=true, globeDirty_=true;
};
} // namespace planet

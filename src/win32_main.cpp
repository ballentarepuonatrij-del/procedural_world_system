#include "planet/renderer.hpp"
#include <shellapi.h>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace planet;
struct Options {
    Config config;
    int frames=0;
    bool globe=false, detail=true, wind=true, clouds=true, water=true, erosion=true, paused=false, windTracers=false;
    double weatherSeconds=0, rainMmH=0,walkSeconds=0;
    bool landforms=true,caves=true,headlamp=true;
    std::optional<Landform> landmark;
    std::optional<double> waterWind,diveDepth;
    bool coast=false,waterMotion=true;
    bool youngFlora=false,plantGrowth=true,grove=false;
    double growDays=0,growthRate=1;
    bool globalWeather=true;
    double globalHours=0;
    std::optional<StormKind> storm;
    WeatherLayer weatherLayer=WeatherLayer::Clouds;
    SurfaceStyle surfaceStyle=SurfaceStyle::Grid;
    std::filesystem::path capture, report;
};
Options parseOptions() {
    int argc=0;
    LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv) throw std::runtime_error("Cannot parse command line");
    Options options;
    try {
        for(int i=1;i<argc;++i) {
            const std::wstring arg=argv[i];
            const auto next=[&]() -> std::wstring {
                if(++i>=argc) throw std::invalid_argument("Missing command-line value");
                return argv[i];
            };
            if(arg==L"--seed") {
                const auto value=next(); std::size_t used=0;
                if(value.empty() || value.front()==L'-') throw std::invalid_argument("Invalid seed");
                options.config.seed=std::stoull(value,&used);
                if(used!=value.size()) throw std::invalid_argument("Invalid seed");
            } else if(arg==L"--frames") {
                const auto value=next(); std::size_t used=0;
                options.frames=std::stoi(value,&used);
                if(used!=value.size() || options.frames<1 || options.frames>100000) throw std::invalid_argument("Invalid frame count");
            } else if(arg==L"--capture") options.capture=next();
            else if(arg==L"--report") options.report=next();
            else if(arg==L"--globe") options.globe=true;
            else if(arg==L"--no-detail") options.detail=false;
            else if(arg==L"--no-weather") options.wind=false;
            else if(arg==L"--no-global-weather") options.globalWeather=false;
            else if(arg==L"--storm") {
                const auto value=next();options.storm=stormKind(std::string(value.begin(),value.end()));
                if(!options.storm) throw std::invalid_argument("Storm must be cyclone, hurricane or thunderstorm");
            }
            else if(arg==L"--global-hours") {
                const auto value=next();std::size_t used=0;options.globalHours=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(options.globalHours) || options.globalHours<0 || options.globalHours>240)
                    throw std::invalid_argument("Global preview must be 0..240 hours");
            }
            else if(arg==L"--weather-layer") {
                const auto value=next();
                if(value==L"clouds") options.weatherLayer=WeatherLayer::Clouds;
                else if(value==L"pressure") options.weatherLayer=WeatherLayer::Pressure;
                else if(value==L"wind") options.weatherLayer=WeatherLayer::Wind;
                else if(value==L"off") options.weatherLayer=WeatherLayer::Off;
                else throw std::invalid_argument("Weather layer must be clouds, pressure, wind or off");
            }
            else if(arg==L"--no-clouds") options.clouds=false;
            else if(arg==L"--no-water") options.water=false;
            else if(arg==L"--no-erosion") options.erosion=false;
            else if(arg==L"--no-landforms") options.landforms=false;
            else if(arg==L"--no-caves") options.caves=false;
            else if(arg==L"--no-headlamp") options.headlamp=false;
            else if(arg==L"--coast") options.coast=true;
            else if(arg==L"--no-water-motion") options.waterMotion=false;
            else if(arg==L"--young-flora") options.youngFlora=true;
            else if(arg==L"--grove") options.grove=true;
            else if(arg==L"--no-growth") options.plantGrowth=false;
            else if(arg==L"--grow-days" || arg==L"--growth-rate") {
                const auto value=next();std::size_t used=0;const double number=std::stod(value,&used);
                const bool rate=arg==L"--growth-rate";
                if(used!=value.size() || !std::isfinite(number) || number<(rate ? 1 : 0) || number>(rate ? 2592000 : 365250))
                    throw std::invalid_argument("Growth days must be 0..365250; rate 1..2592000");
                if(rate) options.growthRate=number; else options.growDays=number;
            }
            else if(arg==L"--wind-m-s" || arg==L"--dive-depth") {
                const auto value=next(); std::size_t used=0; const double number=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(number) || number<0 || number>(arg==L"--wind-m-s" ? 40 : 200))
                    throw std::invalid_argument("Water wind must be 0..40 m/s; dive depth 0..200 m");
                if(arg==L"--wind-m-s") options.waterWind=number; else options.diveDepth=number;
            }
            else if(arg==L"--landmark") {
                const auto value=next(); options.landmark=landmarkType(std::string(value.begin(),value.end()));
                if(!options.landmark) throw std::invalid_argument("Landmark must be cave, canyon, mountains or gorge");
            }
            else if(arg==L"--walk-seconds") {
                const auto value=next(); std::size_t used=0; options.walkSeconds=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(options.walkSeconds) || options.walkSeconds<0 || options.walkSeconds>120)
                    throw std::invalid_argument("Walking preview must be in [0,120] seconds");
            }
            else if(arg==L"--paused") options.paused=true;
            else if(arg==L"--wind-tracers") options.windTracers=true;
            else if(arg==L"--rain-mm-h") {
                const auto value=next(); std::size_t used=0; options.rainMmH=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(options.rainMmH) || options.rainMmH<0 || options.rainMmH>200)
                    throw std::invalid_argument("Rain experiment must be in [0,200] mm/h");
            }
            else if(arg==L"--weather-seconds" || arg==L"--simulate-seconds") {
                const auto value=next(); std::size_t used=0;
                options.weatherSeconds=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(options.weatherSeconds) || options.weatherSeconds<0 || options.weatherSeconds>3600)
                    throw std::invalid_argument("Weather advance must be in [0,3600] seconds");
            }
            else if(arg==L"--surface-style") {
                const auto value=next();
                if(value==L"natural") options.surfaceStyle=SurfaceStyle::Natural;
                else if(value==L"grid") options.surfaceStyle=SurfaceStyle::Grid;
                else if(value==L"wireframe") options.surfaceStyle=SurfaceStyle::Wireframe;
                else throw std::invalid_argument("Surface style must be natural, grid or wireframe");
            }
            else throw std::invalid_argument("Unknown command-line option");
        }
    } catch(...) { LocalFree(argv); throw; }
    LocalFree(argv);
    if(!options.capture.empty() && options.frames==0) options.frames=3;
    return options;
}
World configuredWorld(const Options& options) {
    World world(options.config);auto& modules=world.modules();
    modules.set(Module::SurfaceDetail,options.detail);modules.set(Module::Wind,options.wind);modules.set(Module::Clouds,options.clouds);
    modules.set(Module::Hydrology,options.water);modules.set(Module::Erosion,options.erosion);
    modules.set(Module::Landforms,options.landforms);modules.set(Module::Caves,options.caves);
    modules.set(Module::WaterMotion,options.waterMotion);modules.set(Module::PlantGrowth,options.plantGrowth);
    modules.set(Module::GlobalWeather,options.globalWeather);return world;
}
class Application {
public:
    explicit Application(Options options) : options_(std::move(options)), world_(configuredWorld(options_)), explorer_(world_), simulation_(world_,explorer_.up(),view_.seconds,options_.waterWind,options_.globalHours) {
        view_.seconds+=simulation_.global.elapsed();view_.weatherLayer=options_.weatherLayer;view_.surfaceStyle=options_.surfaceStyle;
        view_.paused=options_.paused; view_.windTracers=options_.windTracers; view_.headlamp=options_.headlamp;
        simulation_.flora.setYoung(options_.youngFlora);simulation_.flora.setRate(options_.growthRate);
        simulation_.flora.advanceDays(world_,options_.growDays);
        simulation_.setRainExperiment(options_.rainMmH);
        simulation_.advance(world_,explorer_.up(),0);
        explorer_.teleport(world_,explorer_.up(),&simulation_.water);
        orientLanding(); noon();
        if(options_.landmark) visitLandmark(*options_.landmark);
        if(options_.grove) visitGrove();
        if(options_.coast || options_.diveDepth) visitCoast(options_.diveDepth.has_value());
        if(options_.storm && !visitStorm(*options_.storm)) throw std::runtime_error("No active storm of the requested kind on this planet");
        simulation_.advance(world_,explorer_.up(),0);
        for(double remaining=options_.weatherSeconds;remaining>1e-9;) {
            const double step=std::min(60.0,remaining);
            simulation_.advance(world_,explorer_.up(),step); view_.seconds+=step; remaining-=step;
        }
        if(options_.diveDepth) {
            const double heading=explorer_.heading();
            explorer_.dive(world_,explorer_.up(),*options_.diveDepth,&simulation_.water,&simulation_.motion);
            explorer_.look(heading,radians(-18));
        }
        for(double left=options_.walkSeconds;left>1e-9;) {
            const double dt=std::min(left,0.1); explorer_.update(world_,{1,0,0,false,false},dt,&simulation_.water,&simulation_.motion,&simulation_.flora);
            simulation_.advance(world_,explorer_.up(),0); left-=dt;
        }
        view_.globe=options_.globe;
    }
    ~Application() {
        captureMouse(false); renderer_.reset();
        if(context_) { wglMakeCurrent(nullptr,nullptr); wglDeleteContext(context_); }
        if(dc_ && window_) ReleaseDC(window_,dc_);
        if(window_) DestroyWindow(window_);
    }
    int run(HINSTANCE instance,int show) {
        WNDCLASSW wc{};
        wc.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
        wc.lpfnWndProc=procedure; wc.hInstance=instance;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=L"PlanetLabWindow";
        if(!RegisterClassW(&wc)) throw std::runtime_error("Cannot register window class");
        RECT rect{0,0,view_.width,view_.height};
        AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        window_=CreateWindowExW(0,wc.lpszClassName,L"PlanetLab - Procedural Planet Simulator",
            WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,
            nullptr,nullptr,instance,this);
        if(!window_) throw std::runtime_error("Cannot create window");
        dc_=GetDC(window_);
        PIXELFORMATDESCRIPTOR format{};
        format.nSize=sizeof(format); format.nVersion=1;
        format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        format.iPixelType=PFD_TYPE_RGBA; format.cColorBits=24; format.cDepthBits=24;
        const int chosen=ChoosePixelFormat(dc_,&format);
        if(!chosen || !SetPixelFormat(dc_,chosen,&format)) throw std::runtime_error("Cannot select OpenGL pixel format");
        context_=wglCreateContext(dc_);
        if(!context_ || !wglMakeCurrent(dc_,context_)) throw std::runtime_error("Cannot create OpenGL context");
        renderer_=std::make_unique<Renderer>(dc_);
        RAWINPUTDEVICE mouse{0x01,0x02,0,window_};
        if(!RegisterRawInputDevices(&mouse,1,sizeof(mouse))) throw std::runtime_error("Cannot register raw mouse input");
        ShowWindow(window_,show); UpdateWindow(window_);
        using Clock=std::chrono::steady_clock;
        auto last=Clock::now();
        int frames=0;
        double renderTime=0, longestRender=0;
        while(running_) {
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_QUIT) running_=false;
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if(!running_) break;
            if(IsIconic(window_)) { Sleep(50); last=Clock::now(); continue; }
            const auto now=Clock::now();
            const double elapsed=std::chrono::duration<double>(now-last).count();
            last=now;
            const double dt=clamp(elapsed,0.0,0.1);
            view_.fps=mix(view_.fps,1/std::max(elapsed,0.001),0.04);
            view_.timeScale=std::min(view_.timeScale,maximumTimeScale());
            const double modelDt=view_.paused ? 0 : dt*view_.timeScale;
            view_.seconds+=modelDt;
            Input input;
            if(view_.captured && !view_.globe) {
                const auto down=[](int key) { return (GetAsyncKeyState(key)&0x8000)!=0; };
                input.forward=static_cast<double>(down('W'))-static_cast<double>(down('S'));
                input.right=static_cast<double>(down('D'))-static_cast<double>(down('A'));
                input.vertical=static_cast<double>(down(VK_SPACE))-static_cast<double>(down(VK_CONTROL));
                input.sprint=down(VK_SHIFT); input.jump=down(VK_SPACE);
            }
            explorer_.update(world_,input,dt,&simulation_.water,&simulation_.motion,&simulation_.flora);
            simulation_.advance(world_,explorer_.up(),modelDt);
            const auto renderStart=Clock::now();
            renderer_->draw(world_,explorer_,simulation_,view_);
            const double renderMs=std::chrono::duration<double,std::milli>(Clock::now()-renderStart).count();
            renderTime+=renderMs; longestRender=std::max(longestRender,renderMs);
            ++frames;
            if(options_.frames>0 && frames>=options_.frames) {
                if(!options_.capture.empty()) renderer_->capture(options_.capture,view_.width,view_.height);
                running_=false;
            }
            const auto error=glGetError();
            if(error!=GL_NO_ERROR) throw std::runtime_error("OpenGL error " + std::to_string(error));
            if(!SwapBuffers(dc_)) throw std::runtime_error("SwapBuffers failed");
            Sleep(8);
        }
        if(!options_.report.empty()) {
            std::ofstream out(options_.report);
            if(!out) throw std::runtime_error("Cannot open smoke report");
            out << "status=ok\nframes=" << frames << "\nrenderer=" << renderer_->device()
                << "\nseed=" << world_.config().seed << "\nlatitude=" << degrees(latitude(explorer_.up()))
                << "\nlongitude=" << degrees(longitude(explorer_.up())) << "\naltitude_m=" << explorer_.altitude()
                << "\nheading_deg=" << degrees(explorer_.heading()) << "\npitch_deg=" << degrees(explorer_.pitch())
                << "\nflight=" << explorer_.flying << "\nglobe=" << view_.globe
                << "\nunderground=" << explorer_.underground() << "\nheadlamp=" << view_.headlamp
                << "\ncave_triangles=" << renderer_->caveCount() << "\nlandform=" << landformName(world_.relief(explorer_.up()).type)
                << "\nsurface_style=" << surfaceStyleName(view_.surfaceStyle)
                << "\ndetail_objects=" << renderer_->detailCount()
                << "\ncloud_sprites=" << renderer_->cloudCount() << "\nweather_steps=" << simulation_.weather.steps()
                << "\nwind_tracers=" << view_.windTracers << "\npaused=" << view_.paused
                << "\nrender_mean_ms=" << renderTime/std::max(1,frames) << "\nrender_max_ms=" << longestRender << '\n';
            out << "flora_days=" << simulation_.flora.days() << "\nflora_rate=" << simulation_.flora.rate()
                << "\nyoung_flora=" << simulation_.flora.young() << "\nflora_panel=" << view_.floraInfo
                << "\nflora_plants=" << renderer_->plantCount() << "\nmarine_plants=" << renderer_->marineCount()
                << "\nflora_species=" << renderer_->plantSpeciesCount() << "\nflora_height_sum_m=" << renderer_->plantHeightSum() << '\n';
            const auto air=simulation_.weather.sample(explorer_.up()); const auto water=simulation_.weather.budget();
            const auto synoptic=simulation_.global.sample(explorer_.up());const auto globalWater=simulation_.global.budget();
            out << "global_enabled=" << simulation_.global.enabled() << "\nglobal_elapsed_s=" << simulation_.global.elapsed()
                << "\nglobal_steps=" << simulation_.global.steps() << "\nglobal_systems=" << simulation_.global.storms().size()
                << "\nglobal_slp_pa=" << synoptic.pressure << "\nglobal_wind10_m_s=" << length(synoptic.velocity)
                << "\nglobal_convection=" << synoptic.convection << "\nglobal_cloud_kg_m2=" << synoptic.cloud
                << "\nglobal_stored_kg=" << globalWater.storedKg << "\nglobal_water_relative_error=" << globalWater.residualKg()/globalWater.initialKg
                << "\nweather_layer=" << weatherLayerName(view_.weatherLayer) << "\nstorm_markers=" << renderer_->stormMarkerCount()
                << "\nrain_streaks=" << renderer_->rainCount() << "\nlightning_segments=" << renderer_->lightningCount()
                << "\nrain_mm_h=" << air.rainRate*3600 << "\nsynoptic_exchange_kg=" << water.synopticExchangeKg << '\n';
            out << "weather_time_s=" << simulation_.weather.seconds() << "\nwind_m_s=" << length(air.velocity)
                << "\nwind_east_m_s=" << air.wind.east << "\nwind_north_m_s=" << air.wind.north
                << "\nrelative_humidity=" << air.humidity << "\ncloud_water_kg_m2=" << air.cloudWater
                << "\nwater_stored_kg=" << water.storedKg << "\nwater_budget_error_kg=" << water.residualKg() << '\n';
            const auto runoff=simulation_.water.budget(); const auto ground=simulation_.water.sample(explorer_.up());
            const auto liquid=liquidAt(world_,&simulation_.water,&simulation_.motion,explorer_.up());
            const double diveDepth=liquid.present ? std::max(0.0,liquid.level-explorer_.altitude()) : 0;
            out << "swimming=" << explorer_.swimming() << "\nunderwater=" << explorer_.underwater()
                << "\nwater_surface_m=" << liquid.level << "\nliquid_depth_m=" << liquid.depth << "\neye_depth_m=" << diveDepth
                << "\nwater_pressure_Pa=" << waterPressure(world_.sample(explorer_.up(),view_.seconds).pressure,diveDepth,liquid.ocean,world_.config().gravity)
                << "\nwave_hs_m=" << simulation_.motion.significantHeight() << "\nwave_time_s=" << simulation_.motion.seconds()
                << "\nwater_wind_m_s=" << length(simulation_.motion.forcingWind())
                << "\nwater_wind_experiment_m_s=" << simulation_.motion.windExperiment().value_or(-1)
                << "\ncurrent_east_m_s=" << dot(liquid.current,east(explorer_.up()))
                << "\ncurrent_north_m_s=" << dot(liquid.current,north(explorer_.up())) << '\n';
            double maximumDepth=0,maximumChange=0; std::size_t wetCells=0;
            for(const auto& c:simulation_.water.grid().cells) if(!c.sea && !c.masked) {
                maximumDepth=std::max(maximumDepth,c.water); maximumChange=std::max(maximumChange,std::abs(c.soil-c.initialSoil));
                if(c.water>0.001) ++wetCells;
            }
            out << "runoff_steps=" << simulation_.water.steps() << "\nexperiment_rain_mm_h=" << simulation_.rainExperiment()
                << "\nwater_triangles=" << renderer_->waterCount() << "\nwater_tracers=" << view_.waterTracers
                << "\nwater_depth_m=" << ground.depth << "\nmaximum_depth_m=" << maximumDepth << "\nwet_cells=" << wetCells
                << "\nmaximum_bed_change_m=" << maximumChange << "\nrunoff_rain_m3=" << runoff.rainM3
                << "\nrunoff_stored_m3=" << runoff.storedWaterM3 << "\nrunoff_boundary_m3=" << runoff.boundaryM3
                << "\nrunoff_water_error_m3=" << runoff.waterResidual() << "\nrunoff_solid_error_kg=" << runoff.solidResidual()
                << "\neroded_kg=" << runoff.erodedKg << "\ndeposited_kg=" << runoff.depositedKg << '\n';
            for(unsigned i=0;i<moduleCount;++i) out << moduleNames[i] << '=' << world_.modules().enabled(static_cast<Module>(i)) << '\n';
            if(!out) throw std::runtime_error("Cannot write smoke report");
        }
        return 0;
    }
private:
    bool visitStorm(StormKind kind) {
        if(!simulation_.global.strongest(kind)) return false;
        const auto p=simulation_.global.observation(kind);
        explorer_.teleport(world_,p);simulation_.advance(world_,p,0);explorer_.teleport(world_,p,&simulation_.water);
        if(world_.climatology(p).ocean) {
            explorer_.flying=true;const double speed=explorer_.flightSpeed;explorer_.flightSpeed=20;
            explorer_.update(world_,{0,0,1,false,false},1,&simulation_.water,&simulation_.motion);explorer_.flightSpeed=speed;
        }
        explorer_.look(pi/2,radians(kind==StormKind::Thunderstorm ? 12 : -3));
        view_.globe=false;noon();if(renderer_) renderer_->invalidate();return true;
    }
    void visitGrove() {
        const auto place=findGrove(world_);
        explorer_.teleport(world_,place.position);simulation_.advance(world_,place.position,0);
        explorer_.teleport(world_,place.position,&simulation_.water);
        const auto look=normalized(place.lookToward-place.position);
        explorer_.look(std::atan2(dot(look,east(place.position)),dot(look,north(place.position))),radians(3));
        view_.globe=false;noon();if(renderer_) renderer_->invalidate();
    }
    double maximumTimeScale() const {
        if(explorer_.swimming()) return 1;
        if(world_.modules().enabled(Module::WaterMotion)) return 120;
        if(world_.modules().enabled(Module::Hydrology)) return Hydrology::maximumTimeScale;
        return world_.modules().enabled(Module::Wind) ? Weather::maximumTimeScale : 86400.0;
    }
    void visitCoast(bool offshore=false) {
        const auto place=world_.findCoast(); const auto p=offshore ? place.lookToward : place.position;
        explorer_.teleport(world_,p); simulation_.advance(world_,p,0); explorer_.teleport(world_,p,&simulation_.water);
        const auto look=normalized(place.lookToward-place.position);
        explorer_.look(std::atan2(dot(look,east(p)),dot(look,north(p))),radians(-5));
        view_.globe=false; noon(); if(renderer_) renderer_->invalidate();
    }
    void visitLandmark(Landform type) {
        const auto place=world_.findLandmark(type,world_.findLand());
        explorer_.teleport(world_,place.position); simulation_.advance(world_,explorer_.up(),0);
        explorer_.teleport(world_,place.position,&simulation_.water);
        const Vec3 look=normalized(place.lookToward-place.position*dot(place.lookToward,place.position));
        const double bearing=std::atan2(dot(look,east(place.position)),dot(look,north(place.position)));
        const double distance=length(place.lookToward-place.position)*world_.config().radius;
        const double rise=world_.elevation(place.lookToward)-explorer_.altitude()-distance*distance/(2*world_.config().radius);
        const double pitch=type==Landform::Karst ? radians(-3) : clamp(std::atan2(rise,std::max(1.0,distance))*0.65,radians(-45),radians(30));
        explorer_.look(bearing,pitch);
        view_.globe=false; noon(); if(renderer_) renderer_->invalidate();
    }
    void orientLanding() {
        const Vec3 up=explorer_.up();
        double best=1e20,bearing=0;
        for(int i=0;i<32;++i) {
            const double angle=tau*i/32;
            const Vec3 forward=north(up)*std::cos(angle)+east(up)*std::sin(angle);
            double obstruction=-1e20;
            for(const double distance:{300.,2000.,12000.}) {
                const auto p=normalized(up+forward*(distance/world_.config().radius));
                obstruction=std::max(obstruction,(world_.surface(p)-explorer_.altitude())/distance);
            }
            if(obstruction<best) { best=obstruction; bearing=angle; }
        }
        explorer_.look(bearing,-radians(3));
    }
    void noon() {
        const double start=std::floor(view_.seconds/86400)*86400;
        double best=-2, bestTime=start;
        for(int i=0;i<288;++i) {
            const double time=start+i*300;
            const double elevation=dot(explorer_.up(),orbit(world_.config(),time).sunDirection);
            if(elevation>best) { best=elevation; bestTime=time; }
        }
        view_.seconds=bestTime;
        simulation_.setClock(bestTime);
        if(renderer_) renderer_->invalidate();
    }
    void captureMouse(bool captured) {
        if(captured==view_.captured) return;
        view_.captured=captured;
        if(captured) {
            SetCapture(window_);
            RECT rect; GetClientRect(window_,&rect);
            POINT top{rect.left,rect.top},bottom{rect.right,rect.bottom};
            ClientToScreen(window_,&top); ClientToScreen(window_,&bottom);
            RECT screen{top.x,top.y,bottom.x,bottom.y}; ClipCursor(&screen);
            ShowCursor(FALSE);
        } else { ReleaseCapture(); ClipCursor(nullptr); ShowCursor(TRUE); }
    }
    void key(WPARAM key) {
        if(key==VK_ESCAPE) captureMouse(false);
        else if(key=='F') explorer_.flying=!explorer_.flying;
        else if(key==VK_TAB) view_.globe=!view_.globe;
        else if(key=='H') view_.help=!view_.help;
        else if(key=='V') view_.windTracers=!view_.windTracers;
        else if(key=='U') view_.waterTracers=!view_.waterTracers;
        else if(key=='L') view_.headlamp=!view_.headlamp;
        else if(key=='E') view_.floraInfo=!view_.floraInfo;
        else if(key=='I') world_.modules().toggle(Module::PlantGrowth);
        else if(key=='Q') world_.modules().toggle(Module::GlobalWeather);
        else if(key==VK_F3) view_.weatherLayer=static_cast<WeatherLayer>((static_cast<unsigned>(view_.weatherLayer)+1)%4);
        else if(key==VK_F2) {
            constexpr std::array<StormKind,3> tour{StormKind::Hurricane,StormKind::Cyclone,StormKind::Thunderstorm};
            for(unsigned i=0;i<tour.size();++i) if(visitStorm(tour[stormIndex_++%tour.size()])) break;
        }
        else if(key=='Y') simulation_.flora.setRate(simulation_.flora.rate()<86400 ? 86400 : (simulation_.flora.rate()<2592000 ? 2592000 : 1));
        else if(key=='Z') {
            const double rate=simulation_.flora.rate();simulation_.flora=Flora{};
            simulation_.flora.setYoung(true);simulation_.flora.setRate(rate);renderer_->invalidate();
        }
        else if(key=='O') visitCoast();
        else if(key=='X') visitGrove();
        else if(key=='N') {
            const auto wind=simulation_.motion.windExperiment();
            simulation_.motion.setWindExperiment(!wind ? std::optional<double>{8} : (*wind<12 ? std::optional<double>{16} : std::nullopt));
        }
        else if(key=='J') {
            constexpr std::array<Landform,4> tour{Landform::Karst,Landform::Canyon,Landform::Mountains,Landform::Gorge};
            visitLandmark(tour[landmarkIndex_++%tour.size()]);
        }
        else if(key=='M' || key=='C' || key=='K') {
            world_.modules().toggle(key=='M' ? Module::Landforms : (key=='C' ? Module::Caves : Module::WaterMotion)); renderer_->invalidate();
        }
        else if(key=='B') simulation_.setRainExperiment(simulation_.rainExperiment()<1 ? 30 : (simulation_.rainExperiment()<60 ? 80 : 0));
        else if(key=='G') {
            switch(view_.surfaceStyle) {
                case SurfaceStyle::Grid: view_.surfaceStyle=SurfaceStyle::Wireframe; break;
                case SurfaceStyle::Wireframe: view_.surfaceStyle=SurfaceStyle::Natural; break;
                default: view_.surfaceStyle=SurfaceStyle::Grid; break;
            }
        }
        else if(key=='P') view_.paused=!view_.paused;
        else if(key=='T') noon();
        else if(key==VK_OEM_PLUS || key==VK_ADD) view_.timeScale=std::min(maximumTimeScale(),view_.timeScale*2);
        else if(key==VK_OEM_MINUS || key==VK_SUBTRACT) view_.timeScale=std::max(1.0,view_.timeScale/2);
        else if(key==VK_HOME) { explorer_.teleport(world_,world_.findLand()); simulation_.advance(world_,explorer_.up(),0); explorer_.teleport(world_,explorer_.up(),&simulation_.water); orientLanding(); }
        else if(key=='R') {
            auto config=world_.config(); ++config.seed;
            const auto modules=world_.modules();
            world_=World(config); world_.modules()=modules;
            explorer_=Explorer(world_);
            const double rain=simulation_.rainExperiment(); const auto wind=simulation_.motion.windExperiment();
            const double growth=simulation_.flora.rate();const bool young=simulation_.flora.young();
            simulation_=Simulation(world_,explorer_.up(),view_.seconds,wind); simulation_.setRainExperiment(rain);
            simulation_.flora.setRate(growth);simulation_.flora.setYoung(young);
            explorer_.teleport(world_,explorer_.up(),&simulation_.water); orientLanding(); noon(); renderer_->invalidate();
        } else if((key>='1' && key<='9') || key=='0') {
            const auto module=static_cast<Module>(key=='0' ? 9 : key-'1');
            world_.modules().toggle(module);
            if(module<Module::Wind || module==Module::Hydrology) renderer_->invalidate();
        }
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
        Application* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            const auto* create=reinterpret_cast<CREATESTRUCTW*>(lParam);
            app=static_cast<Application*>(create->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
        }
        if(!app) return DefWindowProcW(window,message,wParam,lParam);
        switch(message) {
            case WM_CLOSE: app->running_=false; return 0;
            case WM_SIZE:
                app->view_.width=std::max(1,static_cast<int>(LOWORD(lParam)));
                app->view_.height=std::max(1,static_cast<int>(HIWORD(lParam))); return 0;
            case WM_GETMINMAXINFO: {
                auto* info=reinterpret_cast<MINMAXINFO*>(lParam);
                info->ptMinTrackSize={960,640}; return 0;
            }
            case WM_ACTIVATEAPP: if(!wParam) app->captureMouse(false); return 0;
            case WM_LBUTTONDOWN: app->captureMouse(true); return 0;
            case WM_KEYDOWN:
                if(!(lParam&(1LL<<30)) && app->renderer_) app->key(wParam);
                return 0;
            case WM_MOUSEWHEEL:
                app->explorer_.flightSpeed=clamp(app->explorer_.flightSpeed*std::pow(1.5,GET_WHEEL_DELTA_WPARAM(wParam)/120.0),5.0,20000.0);
                return 0;
            case WM_INPUT: {
                if(app->view_.captured && !app->view_.globe) {
                    RAWINPUT input{}; UINT size=sizeof(input);
                    if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))==size && input.header.dwType==RIM_TYPEMOUSE && !(input.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE))
                        app->explorer_.look(input.data.mouse.lLastX*0.0022,-input.data.mouse.lLastY*0.0022);
                }
                return DefWindowProcW(window,message,wParam,lParam);
            }
            default: return DefWindowProcW(window,message,wParam,lParam);
        }
    }
    Options options_;
    World world_;
    Explorer explorer_;
    ViewState view_;
    Simulation simulation_;
    HWND window_=nullptr;
    HDC dc_=nullptr;
    HGLRC context_=nullptr;
    std::unique_ptr<Renderer> renderer_;
    bool running_=true;
    std::size_t landmarkIndex_=0;
    std::size_t stormIndex_=0;
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDPIAware();
    Options options;
    try {
        options=parseOptions();
        Application app(options);
        return app.run(instance,show);
    } catch(const std::exception& error) {
        if(!options.report.empty()) { std::ofstream out(options.report); out << "status=error\nmessage=" << error.what() << '\n'; }
        if(options.frames==0) MessageBoxA(nullptr,error.what(),"PlanetLab",MB_OK|MB_ICONERROR);
        return 1;
    }
}

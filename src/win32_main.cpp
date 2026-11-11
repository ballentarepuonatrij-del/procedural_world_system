#include "planet/renderer.hpp"
#include "planet/landscape.hpp"
#include "planet/win32_audio.hpp"
#include "planet/win32_dialogs.hpp"
#include "planet/state.hpp"
#include <shellapi.h>
#include <windowsx.h>
#include <chrono>
#include <fstream>
#include <memory>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace planet;
struct Options {
    Config config;
    Preferences preferences;
    std::filesystem::path settingsPath,saveDirectory;
    bool resumeExplicit=false,noResume=false,noAutosave=false,worldOptions=false;
    std::string settingsWarning;
    int frames=0;
    bool globe=false, detail=true, wind=true, clouds=true, water=true, erosion=true, paused=false, windTracers=false;
    double weatherSeconds=0, rainMmH=0,walkSeconds=0;
    bool landforms=true,caves=true,headlamp=true;
    std::optional<Landform> landmark;
    std::optional<double> waterWind,diveDepth;
    bool coast=false,waterMotion=true;
    bool youngFlora=false,plantGrowth=true,grove=false,meadow=false;
    std::optional<double> floraDay;
    double growDays=0,growthRate=1;
    bool globalWeather=true;
    double globalHours=0;
    std::optional<StormKind> storm;
    WeatherLayer weatherLayer=WeatherLayer::Clouds;
    bool sound=true;
    bool sky=true,shadows=true,lookAtSun=false;
    bool rivers=true,inlandWaters=true,fauna=true,deer=false;
    double faunaSeconds=0,snowSeconds=0;
    bool snow=true,phenomenaEnabled=true;
    std::optional<Phenomenon> phenomenon;
    std::optional<SnowExperiment> snowSite;
    std::optional<Biome> forestSite;
    std::optional<RiverKind> riverSite;
    std::optional<double> solarHour;
    std::optional<std::array<double,2>> look;
    double volume=.55;
    std::filesystem::path audioCapture;
    SurfaceStyle surfaceStyle=SurfaceStyle::Natural;
    std::filesystem::path capture, report;
};
Options parseOptions() {
    int argc=0;
    LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv) throw std::runtime_error("Cannot parse command line");
    Options options;
    try {
        options.settingsPath=defaultPreferencesPath();
        for(int i=1;i<argc;++i) if(std::wstring_view(argv[i])==L"--settings") {
            if(++i>=argc || std::wstring_view(argv[i]).empty()) throw std::invalid_argument("Missing settings path");
            options.settingsPath=argv[i];
        }
        const auto saved=loadPreferences(options.settingsPath);options.preferences=saved.value;options.settingsWarning=saved.warning;
        options.config.seed=saved.value.seed;options.volume=saved.value.volume/100.0;
        options.surfaceStyle=saved.value.grid ? SurfaceStyle::Grid : SurfaceStyle::Natural;
        options.sky=saved.value.sky;options.shadows=saved.value.shadows;
        const auto& modules=saved.value.modules;
        options.detail=modules.requested(Module::SurfaceDetail);options.wind=modules.requested(Module::Wind);
        options.clouds=modules.requested(Module::Clouds);options.water=modules.requested(Module::Hydrology);
        options.erosion=modules.requested(Module::Erosion);options.landforms=modules.requested(Module::Landforms);
        options.caves=modules.requested(Module::Caves);options.waterMotion=modules.requested(Module::WaterMotion);
        options.plantGrowth=modules.requested(Module::PlantGrowth);options.globalWeather=modules.requested(Module::GlobalWeather);
        options.sound=modules.requested(Module::Sound);
        options.rivers=modules.requested(Module::Rivers);options.inlandWaters=modules.requested(Module::InlandWaters);options.fauna=modules.requested(Module::Fauna);options.snow=modules.requested(Module::Snow);options.phenomenaEnabled=modules.requested(Module::Phenomena);
        for(int i=1;i<argc;++i) {
            const std::wstring arg=argv[i];
            constexpr std::array neutral{L"--settings",L"--save-dir",L"--resume",L"--no-resume",L"--no-autosave",L"--frames",L"--capture",L"--report",L"--paused",L"--audio-capture",L"--volume",L"--surface-style",L"--no-sky",L"--no-shadows",L"--no-sound"};
            if(std::none_of(neutral.begin(),neutral.end(),[&](auto value){return arg==value;})) options.worldOptions=true;
            const auto next=[&]() -> std::wstring {
                if(++i>=argc) throw std::invalid_argument("Missing command-line value");
                return argv[i];
            };
            if(arg==L"--settings") { (void)next(); }
            else if(arg==L"--save-dir") options.saveDirectory=next();
            else if(arg==L"--resume") options.resumeExplicit=true;
            else if(arg==L"--no-resume") options.noResume=true;
            else if(arg==L"--no-autosave") options.noAutosave=true;
            else if(arg==L"--seed") {
                const auto value=next();
                if(value.find_first_not_of(L"0123456789")!=value.npos) throw std::invalid_argument("Invalid seed");
                const auto seed=parseSeed(std::string(value.begin(),value.end()));
                if(!seed) throw std::invalid_argument("Invalid seed");
                options.config.seed=*seed;
            } else if(arg==L"--frames") {
                const auto value=next(); std::size_t used=0;
                options.frames=std::stoi(value,&used);
                if(used!=value.size() || options.frames<1 || options.frames>100000) throw std::invalid_argument("Invalid frame count");
            } else if(arg==L"--capture") options.capture=next();
            else if(arg==L"--report") options.report=next();
            else if(arg==L"--globe") options.globe=true;
            else if(arg==L"--no-sky") options.sky=false;
            else if(arg==L"--no-shadows") options.shadows=false;
            else if(arg==L"--no-rivers") options.rivers=false;
            else if(arg==L"--no-inland-waters") options.inlandWaters=false;
            else if(arg==L"--river") {const auto value=next();options.riverSite=riverKind(std::string(value.begin(),value.end()));if(!options.riverSite) throw std::invalid_argument("River must be stream, river or major");}
            else if(arg==L"--look-at-sun") options.lookAtSun=true;
            else if(arg==L"--solar-hour" || arg==L"--look") {
                const auto number=[&]() {const auto value=next();std::size_t used=0;const double n=std::stod(value,&used);
                    if(used!=value.size() || !std::isfinite(n)) throw std::invalid_argument("Invalid sky view option");
                    return n;};
                if(arg==L"--solar-hour") {const double hour=number();if(hour<0 || hour>24) throw std::invalid_argument("Solar hour must be 0..24");options.solarHour=hour;}
                else {const double heading=number(),pitch=number();if(heading<0 || heading>360 || std::abs(pitch)>88) throw std::invalid_argument("Look requires heading 0..360 and pitch -88..88 degrees");options.look=std::array{heading,pitch};}
            }
            else if(arg==L"--no-detail") options.detail=false;
            else if(arg==L"--no-weather") options.wind=false;
            else if(arg==L"--no-sound") options.sound=false;
            else if(arg==L"--audio-capture") options.audioCapture=next();
            else if(arg==L"--volume") {
                const auto value=next();std::size_t used=0;const double volume=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(volume) || volume<0 || volume>100) throw std::invalid_argument("Volume must be 0..100 percent");
                options.volume=volume/100;
            }
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
                else if(value==L"rivers") options.weatherLayer=WeatherLayer::Rivers;
                else if(value==L"currents") options.weatherLayer=WeatherLayer::Currents;
                else if(value==L"off") options.weatherLayer=WeatherLayer::Off;
                else throw std::invalid_argument("Layer must be clouds, pressure, wind, rivers, currents or off");
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
            else if(arg==L"--forest") {
                const auto value=next();options.forestSite=forestType(std::string(value.begin(),value.end()));
                if(!options.forestSite) throw std::invalid_argument("Forest must be jungle, temperate, taiga or mixed");
            }
            else if(arg==L"--meadow") options.meadow=true;
            else if(arg==L"--flora-day") {
                const auto value=next();std::size_t used=0;const double day=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(day) || std::abs(day)>1e7) throw std::invalid_argument("Invalid biological calendar day");
                options.floraDay=day;
            }
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
            else if(arg==L"--snow") {
                const auto value=next();options.snowSite=snowExperiment(std::string(value.begin(),value.end()));
                if(!options.snowSite) throw std::invalid_argument("Snow must be natural, fine, large, blizzard, drifting or thaw");
            }
            else if(arg==L"--phenomenon") {
                const auto value=next();options.phenomenon=phenomenonType(std::string(value.begin(),value.end()));
                if(!options.phenomenon) throw std::invalid_argument("Phenomenon must be natural, rainbow, halo, pillar, aurora, lightning, fog or dust");
            }
            else if(arg==L"--no-phenomena") options.phenomenaEnabled=false;
            else if(arg==L"--no-snow") options.snow=false;
            else if(arg==L"--snow-seconds") {
                const auto value=next();std::size_t used=0;options.snowSeconds=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(options.snowSeconds) || options.snowSeconds<0 || options.snowSeconds>3600)
                    throw std::invalid_argument("Snow preview must be 0..3600 seconds");
            }
            else if(arg==L"--deer") options.deer=true;
            else if(arg==L"--no-fauna") options.fauna=false;
            else if(arg==L"--fauna-seconds") {
                const auto value=next();std::size_t used=0;options.faunaSeconds=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(options.faunaSeconds) || options.faunaSeconds<0 || options.faunaSeconds>3600)
                    throw std::invalid_argument("Fauna preview must be 0..3600 seconds");
            }
            else if(arg==L"--landmark") {
                const auto value=next(); options.landmark=landmarkType(std::string(value.begin(),value.end()));
                if(!options.landmark) throw std::invalid_argument("Landmark must be cave, canyon, mountains, valley, gorge, wetland, pond, lake, bay, cliff, spit or grotto");
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
    if(options.snowSeconds>0 && !options.snowSite) throw std::invalid_argument("--snow-seconds requires --snow");
    if(!options.capture.empty() && options.frames==0) options.frames=3;
    if(options.saveDirectory.empty()) options.saveDirectory=options.settingsPath.parent_path()/L"saves";
    if(options.resumeExplicit && (options.noResume || options.worldOptions)) throw std::invalid_argument("--resume cannot be combined with --no-resume or world-generation options");
    return options;
}
World configuredWorld(const Options& options) {
    World world(options.config);auto& modules=world.modules();
    modules=options.preferences.modules;
    modules.set(Module::SurfaceDetail,options.detail);modules.set(Module::Wind,options.wind);modules.set(Module::Clouds,options.clouds);
    modules.set(Module::Hydrology,options.water);modules.set(Module::Erosion,options.erosion);
    modules.set(Module::Landforms,options.landforms);modules.set(Module::Caves,options.caves);
    modules.set(Module::WaterMotion,options.waterMotion);modules.set(Module::PlantGrowth,options.plantGrowth);
    modules.set(Module::GlobalWeather,options.globalWeather);modules.set(Module::Sound,options.sound);modules.set(Module::Rivers,options.rivers);modules.set(Module::InlandWaters,options.inlandWaters);modules.set(Module::Fauna,options.fauna);modules.set(Module::Snow,options.snow);modules.set(Module::Phenomena,options.phenomenaEnabled);return world;
}
class Application {
public:
    explicit Application(Options options) : options_(std::move(options)), world_(configuredWorld(options_)), explorer_(world_), simulation_(world_,explorer_.up(),view_.seconds,options_.waterWind,options_.globalHours) {
        view_.seconds+=simulation_.global.elapsed();view_.weatherLayer=options_.weatherLayer;view_.surfaceStyle=options_.surfaceStyle;
        view_.volume=options_.volume;view_.gamma=options_.preferences.gamma;view_.brightness=options_.preferences.brightness;
        view_.interfaceVisible=options_.preferences.interfaceVisible;
        view_.skyEnabled=options_.sky;view_.shadowsEnabled=options_.shadows;
        view_.paused=options_.paused; view_.windTracers=options_.windTracers; view_.headlamp=options_.headlamp;
        simulation_.flora.setYoung(options_.youngFlora);simulation_.flora.setRate(options_.growthRate);
        if(options_.floraDay) simulation_.flora.setCalendarDay(*options_.floraDay);
        simulation_.flora.advanceDays(world_,options_.growDays);
        simulation_.setRainExperiment(options_.rainMmH);
        simulation_.advance(world_,explorer_.up(),0);
        explorer_.teleport(world_,explorer_.up(),&simulation_.water);
        orientLanding(); noon();
        if(options_.landmark) visitLandmark(*options_.landmark,options_.diveDepth.has_value());
        if(options_.grove) visitGrove();
        if(options_.forestSite) visitForest(*options_.forestSite);
        if(options_.meadow) visitMeadow();
        if(options_.deer) visitDeer();
        if(options_.snowSite) visitSnow(*options_.snowSite);
        if(options_.phenomenon) visitPhenomenon(*options_.phenomenon);
        if(options_.coast || (options_.diveDepth && !options_.landmark && !options_.riverSite)) visitCoast(options_.diveDepth.has_value());
        if(options_.riverSite) visitRiver(*options_.riverSite,options_.diveDepth.has_value());
        if(options_.storm && !visitStorm(*options_.storm)) throw std::runtime_error("No active storm of the requested kind on this planet");
        simulation_.advance(world_,explorer_.up(),0);
        for(double remaining=options_.weatherSeconds+options_.snowSeconds;remaining>1e-9;) {
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
        if(options_.solarHour) {
            view_.seconds=solarHourTime(world_.config(),explorer_.up(),view_.seconds,*options_.solarHour);
            simulation_.setClock(view_.seconds);
        }
        if(options_.lookAtSun) {
            const auto sun=orbit(world_.config(),view_.seconds).sunDirection;
            const double heading=std::atan2(dot(sun,east(explorer_.up())),dot(sun,north(explorer_.up())));
            explorer_.look(std::remainder(heading-explorer_.heading(),tau),std::asin(clamp(dot(sun,explorer_.up()),-1.0,1.0))-explorer_.pitch());
        }
        if(options_.look) explorer_.look(radians((*options_.look)[0])-explorer_.heading(),radians((*options_.look)[1])-explorer_.pitch());
        view_.globeCamera.center(explorer_.up());
        simulation_.fauna.advance(world_,explorer_.up(),options_.faunaSeconds,explorer_.altitude(),!view_.globe && !explorer_.underground(),false);
        if(!options_.noResume && (options_.resumeExplicit || (!options_.frames && !options_.worldOptions))) {
            try {if(slotExists(autoSlot)) {loadGame(autoSlot);autoLoaded_=true;if(options_.paused)view_.paused=true;}}
            catch(const std::exception& error) {stateWarning_=error.what();autoSaveBlocked_=true;}
        }
    }
    ~Application() {
        audio_.reset();
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
        MONITORINFO monitor{};monitor.cbSize=sizeof(MONITORINFO);
        if(!GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&monitor)) throw std::runtime_error("Cannot read monitor work area");
        const auto& work=monitor.rcWork;
        initialLayout_=defaultWindowLayout(work.left,work.top,work.right-work.left,work.bottom-work.top);
        window_=CreateWindowExW(0,wc.lpszClassName,L"PlanetLab - Procedural Planet Simulator",
            WS_OVERLAPPEDWINDOW,initialLayout_.x,initialLayout_.y,initialLayout_.width,initialLayout_.height,
            nullptr,nullptr,instance,this);
        if(!window_) throw std::runtime_error("Cannot create window");
        dc_=GetDC(window_);
        PIXELFORMATDESCRIPTOR format{};
        format.nSize=sizeof(format); format.nVersion=1;
        format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        format.iPixelType=PFD_TYPE_RGBA; format.cColorBits=24; format.cDepthBits=24;format.cStencilBits=8;
        const int chosen=ChoosePixelFormat(dc_,&format);
        if(!chosen || !SetPixelFormat(dc_,chosen,&format)) throw std::runtime_error("Cannot select OpenGL pixel format");
        context_=wglCreateContext(dc_);
        if(!context_ || !wglMakeCurrent(dc_,context_)) throw std::runtime_error("Cannot create OpenGL context");
        renderer_=std::make_unique<Renderer>(dc_);
        audio_=std::make_unique<WavePlayer>(!options_.audioCapture.empty());
        audio_->update(soundScene(world_,simulation_,explorer_,view_.volume,view_.paused,view_.globe));
        RAWINPUTDEVICE mouse{0x01,0x02,0,window_};
        if(!RegisterRawInputDevices(&mouse,1,sizeof(mouse))) throw std::runtime_error("Cannot register raw mouse input");
        ShowWindow(window_,show==SW_SHOWMAXIMIZED ? SW_SHOWNORMAL : show); UpdateWindow(window_);
        if(!stateWarning_.empty() && !options_.frames) MessageBoxW(window_,L"Не удалось загрузить автосохранение и его резервную копию. Открыт исходный мир; повреждённый слот не будет перезаписан в этом сеансе. Можно загрузить другой слот или выбрать новый Seed.",L"PlanetLab — Автосохранение",MB_OK|MB_ICONWARNING);
        if(!options_.settingsWarning.empty() && options_.frames==0)
            MessageBoxW(window_,L"Не удалось прочитать настройки. Использованы стандартные значения. Исходный файл будет заменён только при нажатии ОК в настройках.",L"PlanetLab — Настройки",MB_OK|MB_ICONWARNING);
        using Clock=std::chrono::steady_clock;
        auto last=Clock::now();
        int frames=0;
        double renderTime=0, longestRender=0;
        constexpr std::array<const char*,17> phaseNames{"navigation","simulation","audio","render","swap","terrain_build","plants_build","shadow_build","shadows","water","rivers","weather","sky","fauna","snow","phenomena","frame_work"};
        std::array<double,17> phaseTotals{},steadyTotals{},phasePeaks{};
        std::vector<double> steadyFrames;
        unsigned steadyCount=0;
        const auto milliseconds=[](Clock::time_point a,Clock::time_point b) {return std::chrono::duration<double,std::milli>(b-a).count();};
        while(running_) {
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_QUIT) running_=false;
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if(!running_) break;
            if(IsIconic(window_)) {SoundScene quiet;audio_->update(quiet);Sleep(50);last=Clock::now();continue;}
            const auto now=Clock::now();
            if(now>noticeUntil_) view_.notice.clear();
            if(resetFrameClock_) {last=now;resetFrameClock_=false;}
            const double elapsed=std::chrono::duration<double>(now-last).count();
            last=now;
            const double dt=clamp(elapsed,0.0,0.1);
            view_.frameSeconds=dt;
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
            const auto navigationStart=Clock::now();
            explorer_.update(world_,input,view_.globe ? 0 : dt,&simulation_.water,&simulation_.motion,&simulation_.flora);
            const auto simulationStart=Clock::now();
            simulation_.water.snow.setObserved(!view_.globe && !explorer_.underground() &&
                explorer_.altitude()-world_.surface(explorer_.up())<350 && (!explorer_.flying || explorer_.flightSpeed<2000));
            simulation_.advance(world_,explorer_.up(),modelDt);
            const double faunaDt=world_.modules().enabled(Module::Fauna) ? modelDt : 0;
            simulation_.fauna.advance(world_,explorer_.up(),faunaDt,explorer_.altitude(),!view_.globe && !explorer_.underground());
            const auto audioStart=Clock::now();
            auto acoustics=soundScene(world_,simulation_,explorer_,view_.volume,view_.paused,view_.globe);
            if(!focused_) acoustics.audible=false;
            audio_->update(acoustics);view_.audioAvailable=audio_->available();
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
            const auto swapStart=Clock::now();
            if(!SwapBuffers(dc_)) throw std::runtime_error("SwapBuffers failed");
            if(!options_.report.empty()) {
                const auto& r=renderer_->timings();
                std::array<double,17> sample{milliseconds(navigationStart,simulationStart),milliseconds(simulationStart,audioStart),milliseconds(audioStart,renderStart),renderMs,milliseconds(swapStart,Clock::now()),r.terrain,r.plants,r.shadowBuild,r.shadows,r.water,r.rivers,r.weather,r.sky,r.fauna,r.snow,r.phenomena,0};
                for(unsigned i=0;i<5;++i) sample.back()+=sample[i];
                for(std::size_t i=0;i<sample.size();++i) {
                    phaseTotals[i]+=sample[i];if(frames>5) {steadyTotals[i]+=sample[i];phasePeaks[i]=std::max(phasePeaks[i],sample[i]);}
                }
                if(frames>5) {++steadyCount;if(steadyFrames.size()<10000) steadyFrames.push_back(sample.back());}
            }
            Sleep(8);
        }
        autosave();
        audio_->stop();if(!options_.audioCapture.empty()) audio_->saveCapture(options_.audioCapture);
        if(!options_.report.empty()) {
            std::ofstream out(options_.report);
            if(!out) throw std::runtime_error("Cannot open smoke report");
            out << "terrain_async_started=" << renderer_->terrainJobs() << "\nterrain_async_uploaded=" << renderer_->terrainUploads()
                << "\nterrain_async_discarded=" << renderer_->terrainDiscarded() << "\nterrain_spacing_m=" << renderer_->surfaceSpacing() << '\n';
            out << "shadow_async_started=" << renderer_->shadowJobs() << "\nshadow_async_uploaded=" << renderer_->shadowUploads() << '\n';
            for(std::size_t i=0;i<phaseNames.size();++i) out << "profile_" << phaseNames[i] << "_mean_ms=" << phaseTotals[i]/std::max(1,frames)
                << "\nprofile_" << phaseNames[i] << "_steady_ms=" << steadyTotals[i]/std::max(1U,steadyCount)
                << "\nprofile_" << phaseNames[i] << "_max_ms=" << phasePeaks[i] << '\n';
            if(!steadyFrames.empty()) {
                std::sort(steadyFrames.begin(),steadyFrames.end());
                out << "profile_samples=" << steadyFrames.size() << "\nprofile_frame_p50_ms=" << steadyFrames[steadyFrames.size()/2]
                    << "\nprofile_frame_p95_ms=" << steadyFrames[(steadyFrames.size()-1)*95/100] << "\nprofile_frame_max_ms=" << steadyFrames.back() << '\n';
            }
            out << "status=ok\nframes=" << frames << "\nrenderer=" << renderer_->device()
                << "\nseed=" << world_.config().seed << "\nlatitude=" << degrees(latitude(explorer_.up()))
                << "\nlongitude=" << degrees(longitude(explorer_.up())) << "\naltitude_m=" << explorer_.altitude()
                << "\nheading_deg=" << degrees(explorer_.heading()) << "\npitch_deg=" << degrees(explorer_.pitch())
                << "\nflight=" << explorer_.flying << "\nglobe=" << view_.globe
                << "\ncursor_captured=" << view_.captured << "\ninterface_visible=" << view_.interfaceVisible
                << "\nglobe_latitude=" << degrees(latitude(view_.globeCamera.outward)) << "\nglobe_longitude=" << degrees(longitude(view_.globeCamera.outward))
                << "\nsettings_opened=" << settingsOpened_ << "\nsettings_saved=" << settingsSaved_
                << "\nteleport_requests=" << teleportRequests_ << "\nteleports_confirmed=" << teleportsConfirmed_ << "\nseed_changes=" << seedChanges_
                << "\nunderground=" << explorer_.underground() << "\nheadlamp=" << view_.headlamp
                << "\ncave_triangles=" << renderer_->caveCount() << "\nlandform=" << landformName(world_.relief(explorer_.up()).type)
                << "\nsurface_style=" << surfaceStyleName(view_.surfaceStyle)
                << "\ndetail_objects=" << renderer_->detailCount()
                << "\ncloud_sprites=" << renderer_->cloudCount() << "\nweather_steps=" << simulation_.weather.steps()
                << "\nwind_tracers=" << view_.windTracers << "\npaused=" << view_.paused
                << "\nrender_mean_ms=" << renderTime/std::max(1,frames) << "\nrender_max_ms=" << longestRender << '\n';
            out << "flora_days=" << simulation_.flora.days() << "\nflora_rate=" << simulation_.flora.rate()
                << "\nyoung_flora=" << simulation_.flora.young() << "\nflora_panel=" << view_.floraInfo
                << "\nflora_plants=" << renderer_->plantCount() << "\nmarine_plants=" << renderer_->marineCount() << "\nfreshwater_plants=" << renderer_->freshwaterCount()
                << "\nflora_species=" << renderer_->plantSpeciesCount() << "\nflora_height_sum_m=" << renderer_->plantHeightSum()
                << "\nflora_maximum_height_m=" << renderer_->maximumPlantHeight() << "\nflora_trees=" << renderer_->treeCount() << "\nflora_liana_hosts=" << renderer_->lianaHosts()
                << "\nflora_calendar_day=" << simulation_.flora.calendarDay() << "\nflowering_plants=" << renderer_->floweringCount()
                << "\nbudding_plants=" << renderer_->buddingCount() << "\nseed_head_plants=" << renderer_->seedHeadCount() << '\n';
            const auto habitat=world_.climatology(explorer_.up());
            out << "inland_water=" << habitat.inland << "\nreference_water_level_m=" << habitat.waterLevel << "\nreference_water_depth_m=" << habitat.waterDepth << "\nwetness=" << habitat.wetness << "\nsand=" << habitat.sand << '\n';
            const auto& light=renderer_->illumination();
            const auto river=world_.modules().enabled(Module::Rivers) ? world_.waterNetwork()->sample(explorer_.up()) : RiverSample{};
            out << "river_wet=" << river.wet << "\nriver_width_m=" << river.width << "\nriver_depth_m=" << river.depth
                << "\nriver_level_m=" << river.level << "\nriver_discharge_m3_s=" << river.discharge << "\nriver_speed_m_s=" << river.speed
                << "\nriver_triangles=" << renderer_->riverTriangles() << '\n';
            out << "sky_enabled=" << view_.skyEnabled << "\nshadows_enabled=" << view_.shadowsEnabled
                << "\nshadow_support=" << renderer_->shadowSupport() << "\nshadows_used=" << renderer_->shadowsUsed()
                << "\nshadow_volume_triangles=" << renderer_->shadowCount() << "\nstars=" << renderer_->starCount()
                << "\nsun_elevation_deg=" << degrees(light.elevation) << "\nsun_angular_diameter_deg=" << degrees(light.angularRadius)*2
                << "\nsun_direct=" << light.direct << "\nsky_cloud_cover=" << light.cloudCover
                << "\nstar_visibility=" << light.starVisibility << "\nstarlight_lux=" << light.starlightLux
                << "\nmodel_seconds=" << std::setprecision(15) << view_.seconds << std::setprecision(6) << '\n';
            out << "audio_device_opened=" << audio_->available() << "\naudio_error=" << audio_->error()
                << "\naudio_frames=" << audio_->frames() << "\naudio_submitted_buffers=" << audio_->submitted()
                << "\naudio_completed_buffers=" << audio_->completed() << "\naudio_peak_pcm=" << audio_->peak()
                << "\naudio_thunder_events=" << audio_->thunderCount() << "\naudio_volume=" << view_.volume << '\n';
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
            const auto& phenomenon=renderer_->phenomena();double distantFlash=0;for(const auto& f:phenomenon.flashes) distantFlash+=f.intensity;
            out << "phenomena_enabled=" << phenomenon.enabled << "\nphenomenon=" << phenomenonName(view_.phenomenon)
                << "\nrainbow=" << phenomenon.rainbow << "\nhalo=" << phenomenon.halo << "\npillar=" << phenomenon.pillar
                << "\naurora=" << phenomenon.aurora << "\ndistant_lightning=" << distantFlash << "\nfog=" << phenomenon.fog << "\ndust=" << phenomenon.dust
                << "\nphenomena_visibility_m=" << phenomenon.visibility << "\noptical_triangles=" << renderer_->opticalTriangles() << "\ndust_particles=" << renderer_->dustCount()
                << "\nmagnetic_latitude_deg=" << degrees(phenomenon.magneticLatitude) << "\ndew_point_K=" << phenomenon.dewPointK << '\n';
            const auto frozen=simulation_.water.snow.sample(explorer_.up());const auto snowBudget=simulation_.water.snow.budget();
            const auto snowAir=simulation_.water.snow.atmosphere(world_,simulation_.weather,explorer_.up(),world_.surface(explorer_.up()));
            out << "snow_depth_m=" << frozen.depth << "\nsnow_swe_mm=" << frozen.swe*1000 << "\nsnow_particles=" << renderer_->snowCount()
                << "\nsnow_coarse_particles=" << renderer_->coarseSnowCount() << "\nsnow_blowing_particles=" << renderer_->blowingSnowCount()
                << "\nsnow_steps=" << simulation_.water.snow.steps() << "\nsnowfall_mm_h=" << snowAir.fallingMmH
                << "\nsnow_visibility_m=" << snowAir.visibility << "\nsnow_melt_m3=" << snowBudget.meltM3
                << "\nsnow_transport_m3=" << snowBudget.redistributedM3 << "\nsnow_budget_residual_m3=" << snowBudget.residual()
                << "\nsnow_experiment=" << snowExperimentName(simulation_.water.snow.experiment()) << '\n';
            const auto deer=simulation_.fauna.visible(explorer_.up());std::array<unsigned,5> activities{};double nearestDeer=1e30;unsigned herds=0;
            for(const auto& d:deer) {++activities[static_cast<unsigned>(d.activity)];nearestDeer=std::min(nearestDeer,length(d.up-explorer_.up())*world_.config().radius);}
            for(const auto& h:simulation_.fauna.herds()) if(h.active) ++herds;
            out << "fauna_seconds=" << simulation_.fauna.elapsed() << "\nfauna_steps=" << simulation_.fauna.steps()
                << "\ndeer_visible=" << renderer_->deerCount() << "\ndeer_nearby=" << deer.size() << "\ndeer_herds=" << herds
                << "\ndeer_nearest_m=" << (deer.empty() ? -1 : nearestDeer) << '\n';
            for(unsigned i=0;i<activities.size();++i) out << "deer_activity_" << i << '=' << activities[i] << '\n';
            for(unsigned i=0;i<moduleCount;++i) out << moduleNames[i] << '=' << world_.modules().enabled(static_cast<Module>(i)) << '\n';
            RECT windowRect{};GetWindowRect(window_,&windowRect);
            out << "gamma=" << view_.gamma << "\nbrightness=" << view_.brightness << "\ntone_backend=" << renderer_->toneBackend()
                << "\nsaves=" << saves_ << "\nloads=" << loads_ << "\nlast_save_slot=" << lastSaveSlot_ << "\nlast_load_slot=" << lastLoadSlot_
                << "\nauto_loaded=" << autoLoaded_ << "\nauto_saved=" << autoSaved_ << "\nauto_save_blocked=" << autoSaveBlocked_ << "\nrecovered_save=" << recoveredSave_
                << "\nstate_warning=" << stateWarning_ << "\nwindow_x=" << windowRect.left << "\nwindow_y=" << windowRect.top
                << "\nwindow_width=" << windowRect.right-windowRect.left << "\nwindow_height=" << windowRect.bottom-windowRect.top
                << "\nheading=" << explorer_.heading() << "\npitch=" << explorer_.pitch() << '\n';
            if(!out) throw std::runtime_error("Cannot write smoke report");
        }
        return 0;
    }
private:
    Preferences currentPreferences() const {
        Preferences p;p.modules=world_.modules();p.grid=view_.surfaceStyle==SurfaceStyle::Grid;
        p.interfaceVisible=view_.interfaceVisible;p.volume=static_cast<unsigned>(std::lround(view_.volume*100));
        p.sky=view_.skyEnabled;p.shadows=view_.shadowsEnabled;p.gamma=view_.gamma;p.brightness=view_.brightness;
        p.seed=world_.config().seed;return p;
    }
    void endGlobeDrag() {
        globeDragging_=false;globeClick_.reset();
        if(!view_.captured && GetCapture()==window_) ReleaseCapture();
    }
    void modalAction(const std::function<void()>& action) {
        endGlobeDrag();captureMouse(false);
        if(audio_) audio_->update(soundScene(world_,simulation_,explorer_,view_.volume,true,view_.globe));
        try {action();}
        catch(const std::exception& error) {MessageBoxA(window_,error.what(),"PlanetLab",MB_OK|MB_ICONERROR);}
        resetFrameClock_=true;
    }
    void applyDisplay(const Preferences& next) {
        view_.surfaceStyle=next.grid ? SurfaceStyle::Grid : SurfaceStyle::Natural;
        view_.interfaceVisible=next.interfaceVisible;view_.volume=next.volume/100.0;
        view_.skyEnabled=next.sky;view_.shadowsEnabled=next.shadows;view_.gamma=next.gamma;view_.brightness=next.brightness;
    }
    SessionState sessionState() const {
        SessionState state;state.seconds=view_.seconds;state.timeScale=view_.timeScale;state.phenomenonEpoch=view_.phenomenonEpoch;
        state.paused=view_.paused;state.globe=view_.globe;state.help=view_.help;state.headlamp=view_.headlamp;
        state.windTracers=view_.windTracers;state.waterTracers=view_.waterTracers;state.floraInfo=view_.floraInfo;state.faunaInfo=view_.faunaInfo;
        state.weatherLayer=static_cast<unsigned>(view_.weatherLayer);state.phenomenon=view_.phenomenon;
        state.globeOutward=view_.globeCamera.outward;state.globeUp=view_.globeCamera.up;return state;
    }
    bool slotExists(unsigned slot) const {
        const auto path=slotPath(options_.saveDirectory,slot);auto backup=path;backup+=L".bak";
        return std::filesystem::is_regular_file(path) || std::filesystem::is_regular_file(backup);
    }
    void notice(std::string text) {view_.notice=std::move(text);noticeUntil_=std::chrono::steady_clock::now()+std::chrono::seconds(5);}
    void saveGame(unsigned slot) {
        saveState(slotPath(options_.saveDirectory,slot),world_,explorer_,simulation_,sessionState());
        ++saves_;lastSaveSlot_=static_cast<int>(slot);resetFrameClock_=true;
        notice(slot==quickSlot ? "QUICK SAVE COMPLETE" : slot==autoSlot ? "AUTO SAVE COMPLETE" : "SAVED: SLOT "+std::to_string(slot+1));
    }
    void loadGame(unsigned slot) {
        auto next=loadState(slotPath(options_.saveDirectory,slot));
        endGlobeDrag();captureMouse(false);
        world_=std::move(next->world);explorer_=std::move(next->explorer);simulation_=std::move(next->simulation);
        const auto& state=next->session;view_.seconds=state.seconds;view_.timeScale=state.timeScale;view_.phenomenonEpoch=state.phenomenonEpoch;
        view_.paused=state.paused;view_.globe=state.globe;view_.help=state.help;view_.headlamp=state.headlamp;
        view_.windTracers=state.windTracers;view_.waterTracers=state.waterTracers;view_.floraInfo=state.floraInfo;view_.faunaInfo=state.faunaInfo;
        view_.weatherLayer=static_cast<WeatherLayer>(state.weatherLayer);view_.phenomenon=state.phenomenon;
        view_.globeCamera.outward=state.globeOutward;view_.globeCamera.up=state.globeUp;
        if(renderer_) renderer_->invalidate();
        ++loads_;lastLoadSlot_=static_cast<int>(slot);resetFrameClock_=true;autoSaveBlocked_=false;recoveredSave_=next->recovered;
        notice(next->recovered ? "LOADED PREVIOUS BACKUP" : "WORLD RESTORED");
    }
    void autosave(bool quiet=false) {
        if(autoSaved_ || autoSaveBlocked_ || options_.noAutosave || (options_.frames && !options_.resumeExplicit))return;
        try {saveGame(autoSlot);autoSaved_=true;}
        catch(const std::exception& error) {
            stateWarning_=error.what();
            if(!quiet && !options_.frames) MessageBoxW(window_,L"Не удалось записать автосохранение. Предыдущие сохранения оставлены на диске.",L"PlanetLab — Автосохранение",MB_OK|MB_ICONWARNING);
        }
    }
    SlotInfo slotInfo(unsigned slot) const {
        SlotInfo info;info.available=slotExists(slot);
        if(const auto meta=readSaveMetadata(slotPath(options_.saveDirectory,slot))) {
            FILETIME ft{};const auto ticks=meta->savedAt*10000000ULL+116444736000000000ULL;
            ft.dwLowDateTime=static_cast<DWORD>(ticks);ft.dwHighDateTime=static_cast<DWORD>(ticks>>32);
            SYSTEMTIME utc{},local{};FileTimeToSystemTime(&ft,&utc);SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local);
            wchar_t date[40]{};swprintf_s(date,L"%02u.%02u.%04u %02u:%02u",local.wDay,local.wMonth,local.wYear,local.wHour,local.wMinute);
            info.description=std::wstring(date)+L"  Seed "+std::to_wstring(meta->seed);
        } else info.description=info.available ? L"Файл повреждён или несовместим" : L"Пусто";
        return info;
    }
    void openSettings() {
        modalAction([&] {
            ++settingsOpened_;const auto initial=currentPreferences();SettingsActions actions;
            actions.preview=[&](const Preferences& next) {
                applyDisplay(next);renderer_->draw(world_,explorer_,simulation_,view_);SwapBuffers(dc_);
            };
            actions.commit=[&](const Preferences& next) {
                savePreferences(options_.settingsPath,next);world_.modules()=next.modules;applyDisplay(next);
                simulation_.advance(world_,explorer_.up(),0);
                explorer_.update(world_,{},.001,&simulation_.water,&simulation_.motion,&simulation_.flora);
                renderer_->invalidate();++settingsSaved_;
            };
            actions.slot=[&](unsigned slot){return slotInfo(slot);};
            actions.save=[&](unsigned slot){saveGame(slot);};
            actions.load=[&](unsigned slot){loadGame(slot);applyDisplay(initial);};
            const auto result=settingsDialog(window_,initial,actions);
            if(result==SettingsResult::Cancelled) applyDisplay(initial);
        });
    }
    void openSeed() {
        modalAction([&] {
            seedDialog(window_,world_.config().seed,[&](std::uint64_t seed) {
                auto preferences=currentPreferences();preferences.seed=seed;
                if(seed==world_.config().seed) {savePreferences(options_.settingsPath,preferences);return;}
                // Prepare the new world before replacing either live state or saved settings.
                auto config=world_.config();config.seed=seed;World nextWorld(config);nextWorld.modules()=world_.modules();
                Explorer nextExplorer(nextWorld);
                Simulation nextSimulation(nextWorld,nextExplorer.up(),view_.seconds,simulation_.motion.windExperiment());
                nextSimulation.setRainExperiment(simulation_.rainExperiment());
                nextSimulation.flora.setRate(simulation_.flora.rate());nextSimulation.flora.setYoung(simulation_.flora.young());
                savePreferences(options_.settingsPath,preferences);
                world_=std::move(nextWorld);explorer_=std::move(nextExplorer);simulation_=std::move(nextSimulation);
                explorer_.teleport(world_,explorer_.up(),&simulation_.water);orientLanding();noon();
                view_.globeCamera.center(explorer_.up());renderer_->invalidate();++seedChanges_;autoSaveBlocked_=false;
            });
        });
    }
    void globeTravel(int x,int y) {
        if(globeHudHit(view_,x,y)) return;
        const auto target=view_.globeCamera.pick(x,y,view_.width,view_.height);if(!target) return;
        modalAction([&] {
            ++teleportRequests_;
            std::wostringstream text;text << std::fixed << std::setprecision(4)
                << L"Исследователь будет перенесён в выбранную точку.\n\nШирота: " << degrees(latitude(*target))
                << L"°\nДолгота: " << degrees(longitude(*target)) << L"°\n"
                << (world_.climatology(*target).ocean ? L"Место: океан. Вы окажетесь у поверхности воды." : L"Место: суша. Вы окажетесь на поверхности.")
                << L"\n\nПродолжить перемещение?";
            if(MessageBoxW(window_,text.str().c_str(),L"PlanetLab — Перемещение",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) return;
            simulation_.advance(world_,*target,0);explorer_.flying=false;explorer_.teleport(world_,*target,&simulation_.water);
            orientLanding();view_.globe=false;renderer_->invalidate();++teleportsConfirmed_;
        });
    }
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
    void visitPhenomenon(Phenomenon kind) {
        if(kind==Phenomenon::Natural) {view_.phenomenon=kind;return;}
        const auto site=findPhenomenon(world_,kind,view_.seconds);
        explorer_.flying=false;explorer_.teleport(world_,site.position);
        simulation_.advance(world_,site.position,0);explorer_.teleport(world_,site.position,&simulation_.water);
        view_.seconds=site.seconds;simulation_.setClock(site.seconds);
        explorer_.look(std::atan2(dot(site.look,east(site.position)),dot(site.look,north(site.position))),std::asin(clamp(dot(site.look,site.position),-1.,1.)));
        view_.phenomenon=kind;view_.phenomenonEpoch=view_.seconds;view_.globe=false;
        if(renderer_) renderer_->invalidate();
    }
    void visitSnow(SnowExperiment experiment) {
        const auto p=findSnowfield(world_,world_.findLand(),view_.seconds);
        explorer_.flying=false;explorer_.teleport(world_,p);simulation_.water.snow.setObserved(true);
        simulation_.advance(world_,p,0);simulation_.water.snow.setExperiment(experiment);
        explorer_.teleport(world_,p,&simulation_.water);explorer_.look(0,radians(-8));
        view_.globe=false;noon();if(renderer_) renderer_->invalidate();
    }
    void visitDeer() {
        const auto site=findDeerHabitat(world_,world_.findLand());
        if(!simulation_.fauna.focus(world_,site)) throw std::runtime_error("Deer habitat has insufficient clear ground for the herd");
        Vec3 observation=site.home;double best=1e30;
        for(int i=0;i<24;++i) {
            const auto axis=east(site.home)*std::cos(tau*i/24)+north(site.home)*std::sin(tau*i/24);
            const auto p=normalized(site.home+axis*(65/world_.config().radius));const double bed=world_.elevation(p);
            if(world_.surface(p)>bed+.1) continue;
            double score=std::abs(bed-world_.elevation(site.home))*.05;
            for(int j=1;j<8;++j) {
                const double t=j/8.;const auto q=normalized(p*(1-t)+site.home*t);
                score+=std::max(0.,world_.elevation(q)-mix(bed+1.75,world_.elevation(site.home)+.7,t))*100;
            }
            if(score<best) {best=score;observation=p;}
        }
        explorer_.flying=false;explorer_.teleport(world_,observation);simulation_.advance(world_,observation,0);
        explorer_.teleport(world_,observation,&simulation_.water);const auto look=normalized(site.home-observation);
        const double bearing=std::atan2(dot(look,east(observation)),dot(look,north(observation)));
        explorer_.look(bearing,std::atan2(world_.elevation(site.home)+.8-explorer_.altitude(),65));
        view_.globe=false;view_.faunaInfo=true;noon();if(renderer_) renderer_->invalidate();
        simulation_.fauna.advance(world_,observation,0,explorer_.altitude(),true,false);
    }
    void visitMeadow() {
        if(!world_.modules().enabled(Module::Biomes) || !world_.modules().enabled(Module::SurfaceDetail)) {
            if(!window_) throw std::runtime_error("Enable Biomes and Surface detail to visit a meadow");
            MessageBoxW(window_,L"Включите «Биомы» и «Детали поверхности» в настройках.",L"PlanetLab — Луг",MB_OK|MB_ICONINFORMATION);return;
        }
        const auto place=findMeadow(world_);explorer_.teleport(world_,place.position);simulation_.advance(world_,place.position,0);
        explorer_.teleport(world_,place.position,&simulation_.water);const auto look=normalized(place.lookToward-place.position);
        explorer_.look(std::atan2(dot(look,east(place.position)),dot(look,north(place.position))),radians(-31));
        view_.globe=false;view_.floraInfo=true;noon();if(renderer_) renderer_->invalidate();
    }
    void visitForest(Biome kind) {
        const auto place=findForest(world_,kind,view_.seconds);explorer_.flying=false;explorer_.teleport(world_,place.position);
        simulation_.advance(world_,place.position,0);explorer_.teleport(world_,place.position,&simulation_.water);
        const auto look=normalized(place.lookToward-place.position);
        explorer_.look(std::atan2(dot(look,east(place.position)),dot(look,north(place.position))),radians(18));
        view_.globe=false;view_.floraInfo=true;view_.faunaInfo=false;noon();if(renderer_) renderer_->invalidate();
    }
    void visitGrove() {
        const auto place=findGrove(world_);
        explorer_.teleport(world_,place.position);simulation_.advance(world_,place.position,0);
        explorer_.teleport(world_,place.position,&simulation_.water);
        const auto look=normalized(place.lookToward-place.position);
        explorer_.look(std::atan2(dot(look,east(place.position)),dot(look,north(place.position))),radians(3));
        view_.globe=false;noon();if(renderer_) renderer_->invalidate();
    }
    void visitRiver(RiverKind kind,bool inWater=false) {
        if(!world_.modules().enabled(Module::Rivers)) {
            if(!window_) throw std::runtime_error("Enable Rivers to visit a river");
            MessageBoxW(window_,L"Включите модуль «Реки» и «Рельеф» в настройках.",L"PlanetLab — Реки",MB_OK|MB_ICONINFORMATION);return;
        }
        const auto place=world_.waterNetwork()->find(kind,world_.findLand());const auto p=inWater ? place.center : place.bank;
        explorer_.teleport(world_,p);simulation_.advance(world_,p,0);explorer_.teleport(world_,p,&simulation_.water);
        const auto look=normalized(place.center-place.bank+place.downstream*(place.width/world_.config().radius));
        explorer_.look(std::atan2(dot(look,east(p)),dot(look,north(p))),radians(-8));
        view_.globe=false;view_.floraInfo=false;noon();if(renderer_) renderer_->invalidate();
    }
    double maximumTimeScale() const {
        if(explorer_.swimming()) return 1;
        if(world_.modules().enabled(Module::Fauna) || world_.modules().enabled(Module::Snow)) return 120;
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
    void visitLandmark(Landform type,bool submerged=false) {
        auto place=world_.findLandmark(type,world_.findLand());
        if(submerged && inlandLandform(type)) {
            if(const auto patch=world_.landscape()->patch(place.position)) place.position=patch->anchor;
        }
        explorer_.teleport(world_,place.position); simulation_.advance(world_,explorer_.up(),0);
        explorer_.teleport(world_,place.position,&simulation_.water);
        const Vec3 look=normalized(place.lookToward-place.position*dot(place.lookToward,place.position));
        const double bearing=std::atan2(dot(look,east(place.position)),dot(look,north(place.position)));
        const double distance=length(place.lookToward-place.position)*world_.config().radius;
        const double rise=world_.elevation(place.lookToward)-explorer_.altitude()-distance*distance/(2*world_.config().radius);
        const double pitch=(type==Landform::Karst || type==Landform::Grotto) ? radians(-3) : clamp(std::atan2(rise,std::max(1.0,distance))*0.65,radians(-45),radians(30));
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
        } else { if(GetCapture()==window_) ReleaseCapture(); ClipCursor(nullptr); ShowCursor(TRUE); }
    }
    void key(WPARAM key) {
        if((GetKeyState(VK_CONTROL)&0x8000)!=0) {
            if(key=='U') view_.interfaceVisible=!view_.interfaceVisible;
            else if(key=='S') openSeed();
            return;
        }
        if(key==VK_ESCAPE) {
            if(view_.captured) captureMouse(false);
            else openSettings();
        }
        else if(key=='F') explorer_.flying=!explorer_.flying;
        else if(key==VK_TAB) {
            endGlobeDrag();captureMouse(false);view_.globe=!view_.globe;
            if(view_.globe) view_.globeCamera.center(explorer_.up());
        }
        else if(key==VK_F5) {
            try {saveGame(quickSlot);} catch(const std::exception& error) {modalAction([&]{throw std::runtime_error(error.what());});}
        }
        else if(key==VK_F9) modalAction([&]{
            if(!slotExists(quickSlot)) MessageBoxW(window_,L"Быстрый слот пока пуст. Нажмите F5, чтобы сохранить состояние мира.",L"PlanetLab — Быстрая загрузка",MB_OK|MB_ICONINFORMATION);
            else loadGame(quickSlot);
        });
        else if(key=='H') view_.help=!view_.help;
        else if(key=='V') view_.windTracers=!view_.windTracers;
        else if(key=='U') view_.waterTracers=!view_.waterTracers;
        else if(key=='L') view_.headlamp=!view_.headlamp;
        else if(key=='E') {if(view_.faunaInfo) {view_.faunaInfo=false;view_.floraInfo=true;} else if(view_.floraInfo) view_.floraInfo=false;else view_.faunaInfo=true;}
        else if(key==VK_F3) view_.weatherLayer=static_cast<WeatherLayer>((static_cast<unsigned>(view_.weatherLayer)+1)%6);
        else if(key==VK_F6) modalAction([&] {
            constexpr std::array tour{Phenomenon::Rainbow,Phenomenon::Halo,Phenomenon::Pillar,Phenomenon::Aurora,Phenomenon::DistantLightning,Phenomenon::Fog,Phenomenon::Dust,Phenomenon::Natural};
            visitPhenomenon(tour[phenomenonIndex_++%tour.size()]);
        });
        else if(key==VK_F7) {constexpr std::array<RiverKind,3> tour{RiverKind::Major,RiverKind::River,RiverKind::Stream};visitRiver(tour[riverIndex_++%tour.size()]);}
        else if(key==VK_F8) visitMeadow();
        else if(key==VK_F11) modalAction([&] {visitDeer();});
        else if(key==VK_F12) modalAction([&] {
            constexpr std::array<SnowExperiment,6> tour{SnowExperiment::Fine,SnowExperiment::Large,SnowExperiment::Blizzard,SnowExperiment::Drifting,SnowExperiment::Thaw,SnowExperiment::Natural};
            visitSnow(tour[snowIndex_++%tour.size()]);
        });
        else if(key==VK_F4) {
            modalAction([&] {constexpr std::array tour{Landform::Wetland,Landform::Lake,Landform::Pond};visitLandmark(tour[inlandIndex_++%tour.size()]);});
        } else if(key==VK_F10) {
            modalAction([&] {constexpr std::array tour{Landform::Bay,Landform::Spit,Landform::Cliff,Landform::Grotto};visitLandmark(tour[coastIndex_++%tour.size()]);});
        }
        else if(key==VK_F2) {
            constexpr std::array<StormKind,3> tour{StormKind::Hurricane,StormKind::Cyclone,StormKind::Thunderstorm};
            for(unsigned i=0;i<tour.size();++i) if(visitStorm(tour[stormIndex_++%tour.size()])) break;
        }
        else if(key=='Y') simulation_.flora.setRate(simulation_.flora.rate()<86400 ? 86400 : (simulation_.flora.rate()<2592000 ? 2592000 : 1));
        else if(key=='Z') {
            const double rate=simulation_.flora.rate(),calendar=simulation_.flora.calendarDay();simulation_.flora=Flora{};
            simulation_.flora.setYoung(true);simulation_.flora.setRate(rate);simulation_.flora.setCalendarDay(calendar);renderer_->invalidate();
        }
        else if(key=='O') visitCoast();
        else if(key=='X') modalAction([&] {
            constexpr std::array tour{Biome::TemperateRainforest,Biome::Rainforest,Biome::BorealForest,Biome::Forest};
            visitForest(tour[forestIndex_++%tour.size()]);
        });
        else if(key=='N') {
            const auto wind=simulation_.motion.windExperiment();
            simulation_.motion.setWindExperiment(!wind ? std::optional<double>{8} : (*wind<12 ? std::optional<double>{16} : std::nullopt));
        }
        else if(key=='J') {
            constexpr std::array<Landform,5> tour{Landform::Karst,Landform::Canyon,Landform::Mountains,Landform::MountainValley,Landform::Gorge};
            visitLandmark(tour[landmarkIndex_++%tour.size()]);
        }
        else if(key=='B') simulation_.setRainExperiment(simulation_.rainExperiment()<1 ? 30 : (simulation_.rainExperiment()<60 ? 80 : 0));
        else if(key=='P') view_.paused=!view_.paused;
        else if(key=='T') noon();
        else if(key==VK_OEM_PLUS || key==VK_ADD) view_.timeScale=std::min(maximumTimeScale(),view_.timeScale*2);
        else if(key==VK_OEM_MINUS || key==VK_SUBTRACT) view_.timeScale=std::max(1.0,view_.timeScale/2);
        else if(key==VK_HOME) { explorer_.teleport(world_,world_.findLand()); simulation_.advance(world_,explorer_.up(),0); explorer_.teleport(world_,explorer_.up(),&simulation_.water); orientLanding(); }
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
            case WM_QUERYENDSESSION: return TRUE;
            case WM_ENDSESSION: if(wParam) {app->autosave(true);app->running_=false;}return 0;
            case WM_SIZE:
                app->view_.width=std::max(1,static_cast<int>(LOWORD(lParam)));
                app->view_.height=std::max(1,static_cast<int>(HIWORD(lParam))); return 0;
            case WM_GETMINMAXINFO: {
                auto* info=reinterpret_cast<MINMAXINFO*>(lParam);
                info->ptMinTrackSize={std::min(960,app->initialLayout_.width),std::min(640,app->initialLayout_.height)}; return 0;
            }
            case WM_ACTIVATEAPP:
                app->focused_=wParam!=0;
                if(!wParam) {app->captureMouse(false);app->endGlobeDrag();}return 0;
            case WM_CAPTURECHANGED:
                app->globeDragging_=false;app->globeClick_.reset();
                if(app->view_.captured) app->captureMouse(false);
                return 0;
            case WM_LBUTTONDOWN:
                if(app->view_.globe) {
                    if(!app->globeDragging_) app->globeClick_=POINT{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
                } else app->captureMouse(true);
                return 0;
            case WM_LBUTTONUP: {
                const auto start=app->globeClick_;app->globeClick_.reset();
                const int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
                if(app->view_.globe && !app->globeDragging_ && start && std::abs(x-start->x)<=4 && std::abs(y-start->y)<=4)
                    app->globeTravel(x,y);
                return 0;
            }
            case WM_RBUTTONDOWN:
                if(app->view_.globe) {
                    app->globeClick_.reset();app->globeDragging_=true;
                    app->globeMouse_={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};SetCapture(window);
                }
                return 0;
            case WM_RBUTTONUP: app->endGlobeDrag();return 0;
            case WM_MOUSEMOVE:
                if(app->globeDragging_) {
                    if(!(wParam&MK_RBUTTON)) {app->endGlobeDrag();return 0;}
                    const POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
                    app->view_.globeCamera.drag(point.x-app->globeMouse_.x,point.y-app->globeMouse_.y);app->globeMouse_=point;
                }
                return 0;
            case WM_KEYDOWN:
                if(!(lParam&(1LL<<30)) && app->renderer_) app->key(wParam);
                return 0;
            case WM_SYSKEYDOWN:
                if(wParam==VK_F10 && !(lParam&(1LL<<29))) {
                    if(!(lParam&(1LL<<30)) && app->renderer_) app->key(wParam);
                    return 0;
                }
                return DefWindowProcW(window,message,wParam,lParam);
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
    std::unique_ptr<WavePlayer> audio_;
    WindowLayout initialLayout_{0,0,960,640};
    std::chrono::steady_clock::time_point noticeUntil_{};
    std::string stateWarning_;
    bool autoLoaded_=false,autoSaved_=false,autoSaveBlocked_=false,recoveredSave_=false;
    unsigned saves_=0,loads_=0;int lastSaveSlot_=-1,lastLoadSlot_=-1;
    bool running_=true;
    bool focused_=true;
    bool resetFrameClock_=false,globeDragging_=false;
    POINT globeMouse_{};
    std::optional<POINT> globeClick_;
    unsigned inlandIndex_=0,coastIndex_=0;
    unsigned settingsOpened_=0,settingsSaved_=0,teleportRequests_=0,teleportsConfirmed_=0,seedChanges_=0;
    std::size_t landmarkIndex_=0;
    std::size_t stormIndex_=0;
    std::size_t riverIndex_=0,snowIndex_=0,forestIndex_=0,phenomenonIndex_=0;
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

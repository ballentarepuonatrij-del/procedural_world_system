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
    bool globe=false;
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
            else throw std::invalid_argument("Unknown command-line option");
        }
    } catch(...) { LocalFree(argv); throw; }
    LocalFree(argv);
    if(!options.capture.empty() && options.frames==0) options.frames=3;
    return options;
}
class Application {
public:
    explicit Application(Options options) : options_(std::move(options)), world_(options_.config), explorer_(world_) {
        view_.globe=options_.globe; orientLanding(); noon();
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
            if(!view_.paused) view_.seconds+=dt*view_.timeScale;
            Input input;
            if(view_.captured && !view_.globe) {
                const auto down=[](int key) { return (GetAsyncKeyState(key)&0x8000)!=0; };
                input.forward=static_cast<double>(down('W'))-static_cast<double>(down('S'));
                input.right=static_cast<double>(down('D'))-static_cast<double>(down('A'));
                input.vertical=static_cast<double>(down(VK_SPACE))-static_cast<double>(down(VK_CONTROL));
                input.sprint=down(VK_SHIFT); input.jump=down(VK_SPACE);
            }
            explorer_.update(world_,input,dt);
            renderer_->draw(world_,explorer_,view_);
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
                << "\nflight=" << explorer_.flying << "\nglobe=" << view_.globe << '\n';
            for(unsigned i=0;i<5;++i) out << moduleNames[i] << '=' << world_.modules().enabled(static_cast<Module>(i)) << '\n';
            if(!out) throw std::runtime_error("Cannot write smoke report");
        }
        return 0;
    }
private:
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
        else if(key=='P') view_.paused=!view_.paused;
        else if(key=='T') noon();
        else if(key==VK_OEM_PLUS || key==VK_ADD) view_.timeScale=std::min(86400.0,view_.timeScale*2);
        else if(key==VK_OEM_MINUS || key==VK_SUBTRACT) view_.timeScale=std::max(1.0,view_.timeScale/2);
        else if(key==VK_HOME) { explorer_.teleport(world_,world_.findLand()); orientLanding(); }
        else if(key=='R') {
            auto config=world_.config(); ++config.seed;
            const auto modules=world_.modules();
            world_=World(config); world_.modules()=modules;
            explorer_=Explorer(world_); orientLanding(); noon(); renderer_->invalidate();
        } else if(key>='1' && key<='5') {
            world_.modules().toggle(static_cast<Module>(key-'1')); renderer_->invalidate();
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
    HWND window_=nullptr;
    HDC dc_=nullptr;
    HGLRC context_=nullptr;
    std::unique_ptr<Renderer> renderer_;
    bool running_=true;
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

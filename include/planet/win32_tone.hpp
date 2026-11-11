#pragma once
#include <memory>
namespace planet {
// Corrects only the scene framebuffer; never changes the desktop gamma ramp.
class ToneMapper {
public:
    ToneMapper();~ToneMapper();
    void apply(int width,int height,unsigned gamma,unsigned brightness);
    const char* backend() const {return backend_;}
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    const char* backend_="identity";
};
}

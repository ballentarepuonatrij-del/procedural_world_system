#include "planet/win32_tone.hpp"
#include "planet/preferences.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <array>
#include <cstring>
#include <vector>
#include <stdexcept>
namespace planet {
namespace {
template<class T> T entry(const char* name) {
    const auto p=wglGetProcAddress(name);const auto address=reinterpret_cast<std::uintptr_t>(p);
    if(address<=3 || address==~std::uintptr_t{})return nullptr;
    T result{};static_assert(sizeof(result)==sizeof(p));std::memcpy(&result,&p,sizeof(result));return result;
}
}
struct ToneMapper::Impl {
    using CreateShader=GLuint(APIENTRY*)(GLenum);using ShaderSource=void(APIENTRY*)(GLuint,GLsizei,const char* const*,const GLint*);
    using ShaderAction=void(APIENTRY*)(GLuint);using GetStatus=void(APIENTRY*)(GLuint,GLenum,GLint*);
    using CreateProgram=GLuint(APIENTRY*)();using AttachShader=void(APIENTRY*)(GLuint,GLuint);
    using UniformLocation=GLint(APIENTRY*)(GLuint,const char*);using Uniform1i=void(APIENTRY*)(GLint,GLint);using Uniform1f=void(APIENTRY*)(GLint,GLfloat);
    CreateShader createShader=entry<CreateShader>("glCreateShader");ShaderSource shaderSource=entry<ShaderSource>("glShaderSource");
    ShaderAction compileShader=entry<ShaderAction>("glCompileShader"),deleteShader=entry<ShaderAction>("glDeleteShader"),linkProgram=entry<ShaderAction>("glLinkProgram"),useProgram=entry<ShaderAction>("glUseProgram"),deleteProgram=entry<ShaderAction>("glDeleteProgram");
    GetStatus getShader=entry<GetStatus>("glGetShaderiv"),getProgram=entry<GetStatus>("glGetProgramiv");CreateProgram createProgram=entry<CreateProgram>("glCreateProgram");AttachShader attach=entry<AttachShader>("glAttachShader");
    UniformLocation location=entry<UniformLocation>("glGetUniformLocation");Uniform1i integer=entry<Uniform1i>("glUniform1i");Uniform1f real=entry<Uniform1f>("glUniform1f");
    GLuint program=0,texture=0;int width=0,height=0;GLint gammaUniform=-1,brightnessUniform=-1;
    std::vector<unsigned char> pixels;
    Impl() {
        if(!createShader || !shaderSource || !compileShader || !deleteShader || !linkProgram || !useProgram || !deleteProgram || !getShader || !getProgram || !createProgram || !attach || !location || !integer || !real)return;
        const auto compile=[&](GLenum type,const char* source) {const auto shader=createShader(type);shaderSource(shader,1,&source,nullptr);compileShader(shader);GLint ok=0;getShader(shader,0x8B81,&ok);if(!ok){deleteShader(shader);return GLuint{};}return shader;};
        const auto vertex=compile(0x8B31,"#version 120\nvoid main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}");
        const auto fragment=compile(0x8B30,"#version 120\nuniform sampler2D scene;uniform float gammaValue;uniform float brightness;void main(){vec3 c=texture2D(scene,gl_TexCoord[0].st).rgb;gl_FragColor=vec4(pow(clamp(c*brightness,0.0,1.0),vec3(1.0/gammaValue)),1.0);}");
        if(vertex && fragment) {program=createProgram();attach(program,vertex);attach(program,fragment);linkProgram(program);GLint ok=0;getProgram(program,0x8B82,&ok);if(!ok){deleteProgram(program);program=0;}}
        if(vertex)deleteShader(vertex);
        if(fragment)deleteShader(fragment);
        if(program) {gammaUniform=location(program,"gammaValue");brightnessUniform=location(program,"brightness");GLint previous=0;glGetIntegerv(0x8B8D,&previous);useProgram(program);integer(location(program,"scene"),0);useProgram(static_cast<GLuint>(previous));glGenTextures(1,&texture);}
    }
    ~Impl() {if(texture)glDeleteTextures(1,&texture);if(program)deleteProgram(program);}
};
ToneMapper::ToneMapper()=default;
ToneMapper::~ToneMapper()=default;
void ToneMapper::apply(int width,int height,unsigned gamma,unsigned brightness) {
    if(gamma==100 && brightness==100) {backend_="identity";return;}
    (void)displayIntensity(.5,gamma,brightness);
    if(width<1 || height<1 || width>16384 || height>16384)throw std::invalid_argument("Invalid display dimensions");
    if(!impl_)impl_=std::make_unique<Impl>();
    auto& p=*impl_;
    GLint maxTexture=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTexture);const bool gpu=p.program && width<=maxTexture && height<=maxTexture;
    glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_SCISSOR_TEST);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,width,0,height,-1,1);glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();glReadBuffer(GL_BACK);
    if(gpu) {
        backend_="gpu";glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,p.texture);
        if(p.width!=width || p.height!=height) {glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);p.width=width;p.height=height;glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);}
        glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);p.useProgram(p.program);p.real(p.gammaUniform,static_cast<float>(gamma)/100.f);p.real(p.brightnessUniform,static_cast<float>(brightness)/100.f);
        glColor4f(1,1,1,1);glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2i(0,0);glTexCoord2f(1,0);glVertex2i(width,0);glTexCoord2f(1,1);glVertex2i(width,height);glTexCoord2f(0,1);glVertex2i(0,height);glEnd();p.useProgram(static_cast<GLuint>(previous));
    } else {
        backend_="cpu";glDisable(GL_TEXTURE_2D);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        p.pixels.resize(static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*3);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,p.pixels.data());
        std::array<unsigned char,256> lut{};for(unsigned i=0;i<256;++i)lut[i]=static_cast<unsigned char>(std::lround(displayIntensity(i/255.,gamma,brightness)*255));
        for(auto& value:p.pixels)value=lut[value];
        glRasterPos2i(0,0);glDrawPixels(width,height,GL_RGB,GL_UNSIGNED_BYTE,p.pixels.data());
    }
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopClientAttrib();glPopAttrib();
}
}

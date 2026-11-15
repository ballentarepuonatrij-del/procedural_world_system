#include "planet/win32_water.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <cstring>
#include <vector>

namespace planet {
namespace {
template<class T> T entry(const char* name) {
    const auto p=wglGetProcAddress(name);const auto address=reinterpret_cast<std::uintptr_t>(p);
    if(address<=3 || address==~std::uintptr_t{}) return nullptr;
    T result{};static_assert(sizeof(result)==sizeof(p));std::memcpy(&result,&p,sizeof(result));return result;
}
constexpr const char* vertexSource=R"GLSL(#version 120
uniform mat3 basis;
uniform vec3 origin;
varying vec2 iceUV;
varying vec4 surface;
varying float distanceToEye,fogDistance;
void main() {
    gl_Position=ftransform();gl_FrontColor=gl_Color;
    iceUV=(basis*gl_Vertex.xyz+origin).xy/16.0;
    surface=gl_MultiTexCoord0;
    distanceToEye=length((gl_ModelViewMatrix*gl_Vertex).xyz);
    fogDistance=abs((gl_ModelViewMatrix*gl_Vertex).z);
}
)GLSL";
constexpr const char* fragmentSource=R"GLSL(#version 120
uniform sampler2D ripples,fractures;
uniform float river,light,fogMode;
varying vec2 iceUV;
varying vec4 surface;
varying float distanceToEye,fogDistance;
void main() {
    vec4 color=gl_Color;
    if(river>0.5) color.rgb*=mix(texture2D(ripples,surface.xy).r,1.0,surface.z);
    if(surface.z>0.001) {
        vec4 pattern=texture2D(fractures,iceUV);
        float fade=surface.z*(1.0-smoothstep(70.0,200.0,distanceToEye));
        float frost=pattern.g*smoothstep(0.04,0.5,surface.w);
        float white=(pattern.r*0.32+frost*0.14)*fade;
        color.rgb=mix(color.rgb,vec3(0.72,0.90,0.96)*light,white);
        color.a=clamp(color.a*(1.0+white*0.4),0.0,0.99);
    }
    float d=fogDistance*gl_Fog.density;
    float fog=fogMode<0.5 ? 1.0 : exp(fogMode<1.5 ? -d : -d*d);
    gl_FragColor=vec4(mix(gl_Fog.color.rgb,color.rgb,clamp(fog,0.0,1.0)),color.a);
}
)GLSL";
}
struct WaterEffects::Impl {
    using CreateShader=GLuint(APIENTRY*)(GLenum);using ShaderSource=void(APIENTRY*)(GLuint,GLsizei,const char* const*,const GLint*);
    using Action=void(APIENTRY*)(GLuint);using Status=void(APIENTRY*)(GLuint,GLenum,GLint*);
    using CreateProgram=GLuint(APIENTRY*)();using Attach=void(APIENTRY*)(GLuint,GLuint);
    using Location=GLint(APIENTRY*)(GLuint,const char*);using Integer=void(APIENTRY*)(GLint,GLint);using Real=void(APIENTRY*)(GLint,GLfloat);
    using Vector=void(APIENTRY*)(GLint,GLfloat,GLfloat,GLfloat);using Matrix=void(APIENTRY*)(GLint,GLsizei,GLboolean,const GLfloat*);using Active=void(APIENTRY*)(GLenum);
    CreateShader createShader=entry<CreateShader>("glCreateShader");ShaderSource source=entry<ShaderSource>("glShaderSource");
    Action compile=entry<Action>("glCompileShader"),deleteShader=entry<Action>("glDeleteShader"),link=entry<Action>("glLinkProgram"),use=entry<Action>("glUseProgram"),deleteProgram=entry<Action>("glDeleteProgram");
    Status shaderStatus=entry<Status>("glGetShaderiv"),programStatus=entry<Status>("glGetProgramiv");
    CreateProgram createProgram=entry<CreateProgram>("glCreateProgram");Attach attach=entry<Attach>("glAttachShader");
    Location location=entry<Location>("glGetUniformLocation");Integer integer=entry<Integer>("glUniform1i");Real real=entry<Real>("glUniform1f");
    Vector vector=entry<Vector>("glUniform3f");Matrix matrix=entry<Matrix>("glUniformMatrix3fv");Active active=entry<Active>("glActiveTexture");
    GLuint program=0,texture=0;GLint previousProgram=0,previousActive=0,previousTexture=0;bool bound=false;
    GLint basis=-1,origin=-1,light=-1,river=-1,fog=-1;
    ~Impl() {if(program) deleteProgram(program);if(texture) glDeleteTextures(1,&texture);}
    void setup() {
        if(!createShader || !source || !compile || !deleteShader || !link || !use || !deleteProgram || !shaderStatus || !programStatus || !createProgram || !attach || !location || !integer || !real || !vector || !matrix || !active) return;
        const auto shader=[&](GLenum kind,const char* text) {const auto id=createShader(kind);source(id,1,&text,nullptr);compile(id);GLint ok=0;shaderStatus(id,0x8B81,&ok);if(!ok) {deleteShader(id);return GLuint{};}return id;};
        const auto v=shader(0x8B31,vertexSource),f=shader(0x8B30,fragmentSource);
        if(v && f) {program=createProgram();attach(program,v);attach(program,f);link(program);GLint ok=0;programStatus(program,0x8B82,&ok);if(!ok){deleteProgram(program);program=0;}}
        if(v) deleteShader(v);
        if(f) deleteShader(f);
        if(!program) return;
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);use(program);integer(location(program,"ripples"),0);integer(location(program,"fractures"),1);
        basis=location(program,"basis");origin=location(program,"origin");light=location(program,"light");river=location(program,"river");fog=location(program,"fogMode");use(static_cast<GLuint>(previous));
        std::vector<unsigned char> pixels(256*256*3);
        for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
            const double px=(x+.5)/32,py=(y+.5)/32;const int ix=x/32,iy=y/32;double first=100,second=100;
            for(int j=-1;j<=1;++j) for(int i=-1;i<=1;++i) {
                const auto id=static_cast<std::uint64_t>(((iy+j+8)%8)*8+(ix+i+8)%8);
                const double dx=ix+i+.15+.7*randomUnit(hash64(id+129))-px,dy=iy+j+.15+.7*randomUnit(hash64(id+381))-py,d=dx*dx+dy*dy;
                if(d<first) {second=first;first=d;} else second=std::min(second,d);
            }
            const double crack=1-smoothstep(.012,.085,std::sqrt(second)-std::sqrt(first));
            const auto index=static_cast<std::size_t>(y*256+x)*3;
            pixels[index]=static_cast<unsigned char>(std::lround(255*crack));
            pixels[index+1]=static_cast<unsigned char>(std::lround(255*(.3+.7*randomUnit(hash64(index+701)))));pixels[index+2]=0;
        }
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        // RGB row widths are multiples of four until the last levels.
        GLint alignment=0;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        for(int size=256,level=0;;size/=2,++level) {
            glTexImage2D(GL_TEXTURE_2D,level,GL_RGB,size,size,0,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());if(size==1) break;
            std::vector<unsigned char> next(static_cast<std::size_t>(size*size*3/4));
            for(int y=0;y<size/2;++y) for(int x=0;x<size/2;++x) for(int c=0;c<3;++c) next[static_cast<std::size_t>((y*size/2+x)*3+c)]=static_cast<unsigned char>((pixels[static_cast<std::size_t>((2*y*size+2*x)*3+c)]+pixels[static_cast<std::size_t>((2*y*size+2*x+1)*3+c)]+pixels[static_cast<std::size_t>(((2*y+1)*size+2*x)*3+c)]+pixels[static_cast<std::size_t>(((2*y+1)*size+2*x+1)*3+c)])/4);
            pixels=std::move(next);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    }
};
WaterEffects::WaterEffects()=default;WaterEffects::~WaterEffects()=default;
void WaterEffects::initialize(bool enabled) {if(enabled) {impl_=std::make_unique<Impl>();impl_->setup();}}
const char* WaterEffects::backend() const {return impl_ && impl_->program ? "ice-glsl" : "colored-ice";}
void WaterEffects::prepare(Vec3 anchor,Vec3 eastAxis,Vec3 northAxis,Vec3 hydroAnchor,double radius,double light) {
    if(!impl_ || !impl_->program) return;
    auto& p=*impl_;const auto he=east(hydroAnchor),hn=north(hydroAnchor);const auto project=[&](Vec3 v){return Vec3{dot(v,he),dot(v,hn),dot(v,hydroAnchor)};};
    const auto a=project(eastAxis),b=project(anchor),c=project(northAxis),o=project((anchor-hydroAnchor)*radius);
    const GLfloat basis[]{static_cast<float>(a.x),static_cast<float>(a.y),static_cast<float>(a.z),static_cast<float>(b.x),static_cast<float>(b.y),static_cast<float>(b.z),static_cast<float>(c.x),static_cast<float>(c.y),static_cast<float>(c.z)};
    GLint previous=0;glGetIntegerv(0x8B8D,&previous);p.use(p.program);p.matrix(p.basis,1,GL_FALSE,basis);
    p.vector(p.origin,static_cast<float>(std::remainder(o.x,16.)),static_cast<float>(std::remainder(o.y,16.)),0);p.real(p.light,static_cast<float>(light));p.use(static_cast<GLuint>(previous));
}
bool WaterEffects::begin(bool river) {
    if(!impl_ || !impl_->program) return false;
    auto& p=*impl_;glGetIntegerv(0x8B8D,&p.previousProgram);glGetIntegerv(0x84E0,&p.previousActive);
    p.active(0x84C1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&p.previousTexture);glBindTexture(GL_TEXTURE_2D,p.texture);p.active(0x84C0);
    p.use(p.program);p.real(p.river,river ? 1.f : 0.f);GLint fog=0;glGetIntegerv(GL_FOG_MODE,&fog);p.real(p.fog,!glIsEnabled(GL_FOG) ? 0.f : fog==GL_EXP ? 1.f : 2.f);p.bound=true;return true;
}
void WaterEffects::end() {
    if(!impl_ || !impl_->bound) return;
    auto& p=*impl_;p.use(static_cast<GLuint>(p.previousProgram));p.active(0x84C1);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(p.previousTexture));p.active(static_cast<GLenum>(p.previousActive));p.bound=false;
}
} // namespace planet

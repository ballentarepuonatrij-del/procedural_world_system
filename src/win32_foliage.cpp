#include "planet/win32_foliage.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <cstring>
#include <cstddef>

namespace planet {
namespace {
template<class T> T entry(const char* name) {
    const auto p=wglGetProcAddress(name);const auto a=reinterpret_cast<std::uintptr_t>(p);if(a<=3 || a==~std::uintptr_t{}) return nullptr;
    T out{};static_assert(sizeof(out)==sizeof(p));std::memcpy(&out,&p,sizeof(out));return out;
}
constexpr const char* vertexSource=R"GLSL(#version 120
uniform sampler2D bends;
uniform float phase,strength,sunEnabled,lampEnabled,wire,motion;
varying float fogDepth;
void main() {
    vec3 position=gl_Vertex.xyz,normal=gl_Normal;
    float flex=gl_MultiTexCoord0.x,height=gl_MultiTexCoord0.y,seed=gl_MultiTexCoord0.z,slot=gl_MultiTexCoord0.w;
    if(slot>=0.0 && motion>0.5) {
        vec3 bend=(texture2D(bends,vec2((slot+0.5)/256.0,0.5)).rgb*65535.0-32768.0)/32767.0;
        float gust=0.78+0.18*sin(seed+phase)+0.04*sin(seed*2.0+phase*2.0);
        bend*=gust;
        position+=(bend-vec3(0.0,0.5*dot(bend,bend),0.0))*height*flex;
        position+=vec3(0.3,1.0,0.2)*(gl_MultiTexCoord1.x*strength*sin(phase*3.0+gl_MultiTexCoord1.y));
        normal.y-=dot(normal,bend)*2.0*flex;
    }
    vec4 eye=gl_ModelViewMatrix*vec4(position,1.0);
    gl_Position=gl_ProjectionMatrix*eye;fogDepth=abs(eye.z);
    normal=normalize(gl_NormalMatrix*normal);
    vec3 ambient=gl_LightModel.ambient.rgb;
    float diffuse=dot(normal,normalize(gl_LightSource[0].position.xyz));
    vec3 front=ambient+gl_LightSource[0].diffuse.rgb*sunEnabled*max(0.0,diffuse);
    vec3 back=ambient+gl_LightSource[0].diffuse.rgb*sunEnabled*max(0.0,-diffuse);
    if(lampEnabled>0.5) {
        vec3 delta=gl_LightSource[1].position.xyz-eye.xyz;float d=length(delta);
        float n=dot(normal,normalize(delta));
        vec3 light=gl_LightSource[1].diffuse.rgb/(1.0+0.02*d+0.0015*d*d);
        front+=light*max(0.0,n);back+=light*max(0.0,-n);
    }
    vec3 color=wire>0.5 ? vec3(0.018,0.030,0.065) : gl_Color.rgb;
    gl_FrontColor=vec4(wire>1.5 ? vec3(0.12,0.64,0.78) : color*front,1.0);
    gl_BackColor=vec4(wire>1.5 ? vec3(0.12,0.64,0.78) : color*back,1.0);
}
)GLSL";
constexpr const char* fragmentSource=R"GLSL(#version 120
uniform float fogMode;
varying float fogDepth;
void main() {
    float d=fogDepth*gl_Fog.density;
    float fog=fogMode<0.5 ? 1.0 : exp(fogMode<1.5 ? -d : -d*d);
    gl_FragColor=vec4(mix(gl_Fog.color.rgb,gl_Color.rgb,clamp(fog,0.0,1.0)),1.0);
}
)GLSL";
}
struct FoliageRenderer::Impl {
    using CreateShader=GLuint(APIENTRY*)(GLenum);using Source=void(APIENTRY*)(GLuint,GLsizei,const char* const*,const GLint*);
    using Action=void(APIENTRY*)(GLuint);using Status=void(APIENTRY*)(GLuint,GLenum,GLint*);using CreateProgram=GLuint(APIENTRY*)();using Attach=void(APIENTRY*)(GLuint,GLuint);
    using Location=GLint(APIENTRY*)(GLuint,const char*);using Real=void(APIENTRY*)(GLint,GLfloat);using Integer=void(APIENTRY*)(GLint,GLint);
    using Coord=void(APIENTRY*)(GLenum,GLfloat,GLfloat,GLfloat,GLfloat);
    CreateShader createShader=entry<CreateShader>("glCreateShader");Source source=entry<Source>("glShaderSource");
    Action compile=entry<Action>("glCompileShader"),deleteShader=entry<Action>("glDeleteShader"),link=entry<Action>("glLinkProgram"),use=entry<Action>("glUseProgram"),deleteProgram=entry<Action>("glDeleteProgram");
    Status shaderStatus=entry<Status>("glGetShaderiv"),programStatus=entry<Status>("glGetProgramiv");CreateProgram createProgram=entry<CreateProgram>("glCreateProgram");Attach attach=entry<Attach>("glAttachShader");
    Location location=entry<Location>("glGetUniformLocation");Real real=entry<Real>("glUniform1f");Integer integer=entry<Integer>("glUniform1i");Coord coord=entry<Coord>("glMultiTexCoord4f");
    GLuint program=0,texture=0;std::array<GLint,7> uniforms{};
    std::vector<FoliageVertex> vertices,deformed;std::vector<FoliageRange> ranges;
    FoliageDraw view;std::size_t leaves=0,animated=0;
    ~Impl() {if(program) deleteProgram(program);if(texture) glDeleteTextures(1,&texture);}
    void setup() {
        if(!createShader || !source || !compile || !deleteShader || !link || !use || !deleteProgram || !shaderStatus || !programStatus || !createProgram || !attach || !location || !real || !integer || !coord) return;
        GLint units=0;glGetIntegerv(0x8B4C,&units);if(units<1) return;
        const auto shader=[&](GLenum type,const char* text) {const auto id=createShader(type);source(id,1,&text,nullptr);compile(id);GLint ok=0;shaderStatus(id,0x8B81,&ok);if(!ok) {deleteShader(id);return GLuint{};}return id;};
        const auto vertex=shader(0x8B31,vertexSource),fragment=shader(0x8B30,fragmentSource);
        if(vertex && fragment) {program=createProgram();attach(program,vertex);attach(program,fragment);link(program);GLint ok=0;programStatus(program,0x8B82,&ok);if(!ok) {deleteProgram(program);program=0;}}
        if(vertex) deleteShader(vertex);
        if(fragment) deleteShader(fragment);
        if(!program) return;
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);use(program);integer(location(program,"bends"),0);
        constexpr std::array names{"phase","strength","sunEnabled","lampEnabled","wire","fogMode","motion"};
        for(std::size_t i=0;i<names.size();++i) uniforms[i]=location(program,names[i]);
        use(static_cast<GLuint>(previous));
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16,256,1,0,GL_RGBA,GL_UNSIGNED_SHORT,nullptr);
    }
};
FoliageRenderer::FoliageRenderer()=default;
FoliageRenderer::~FoliageRenderer()=default;
void FoliageRenderer::initialize(bool shaders) {impl_=std::make_unique<Impl>();if(shaders) impl_->setup();}
const char* FoliageRenderer::backend() const {return impl_ && impl_->program ? "vertex-plant-springs" : "cpu-bounded";}
std::size_t FoliageRenderer::leafVertices() const {return impl_ ? impl_->leaves : 0;}
std::size_t FoliageRenderer::animatedVertices() const {return impl_ ? impl_->animated : 0;}
void FoliageRenderer::adopt(std::vector<FoliageVertex> vertices,std::vector<FoliageRange> ranges,std::size_t leaves) {
    auto& p=*impl_;p.leaves=leaves;p.ranges=std::move(ranges);
    if(!p.program) {p.vertices=std::move(vertices);p.deformed=p.vertices;}
}
void FoliageRenderer::vertex(const FoliageVertex& v) const {
    glColor3f(v.color.r,v.color.g,v.color.b);glNormal3d(v.normal.x,v.normal.y,v.normal.z);
    if(impl_->program) {
        impl_->coord(0x84C0,v.flex,v.height,v.phase,v.slot);
        impl_->coord(0x84C1,v.flutter,v.leafPhase,0,0);
    }
    glVertex3d(v.position.x,v.position.y,v.position.z);
}
void FoliageRenderer::prepare(const FoliageField& field,const FoliageDraw& view) {
    auto& p=*impl_;p.view=view;p.animated=0;
    if(!view.enabled || field.roots().empty()) return;
    const auto local=[&](Vec3 v){return Vec3{dot(v,view.east),dot(v,view.up),dot(v,view.north)};};
    if(p.program) {
        std::array<unsigned short,maximumFoliagePlants*4> pixels{};pixels.fill(32768);
        for(std::size_t i=0;i<field.roots().size();++i) {
            const auto v=local(field.bend(i));const std::array values{v.x,v.y,v.z};
            for(std::size_t j=0;j<3;++j) pixels[i*4+j]=static_cast<unsigned short>(std::lround(clamp(values[j],-1.,1.)*32767+32768));
        }
        glPushAttrib(GL_TEXTURE_BIT);glBindTexture(GL_TEXTURE_2D,p.texture);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,1,GL_RGBA,GL_UNSIGNED_SHORT,pixels.data());glPopAttrib();
        if(view.enabled) for(const auto& r:p.ranges) if(r.slot>=0) p.animated+=r.count;
    } else {
        // Complete plants are selected in nearest-first mesh order. Distant
        // geometry stays cached; never perform unbounded CPU vertex animation.
        for(const auto& r:p.ranges) {
            if(r.slot<0 || p.animated+r.count>18000) continue;
            const auto bend=local(field.bend(static_cast<std::size_t>(r.slot)));
            for(std::size_t i=r.first;i<r.first+r.count;++i) {
                const auto& v=p.vertices[i];auto& out=p.deformed[i];out=v;
                if(view.enabled) {
                    out.position=bendFoliagePosition(v,bend,view.phase,view.windStrength);
                    const double gust=.78+.18*std::sin(v.phase+view.phase)+.04*std::sin(v.phase*2+view.phase*2);
                    out.normal.y-=dot(v.normal,bend)*gust*2*v.flex;out.normal=normalized(out.normal);
                }
            }
            p.animated+=r.count;
        }
        if(!view.enabled) p.animated=0;
    }
}
void FoliageRenderer::draw(unsigned list,int wire) {
    auto& p=*impl_;
    if(p.animated==0) {glCallList(list);return;}
    glPushAttrib(GL_ENABLE_BIT|GL_LIGHTING_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);glDisable(GL_TEXTURE_2D);
    if(p.program) {
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);p.use(p.program);glBindTexture(GL_TEXTURE_2D,p.texture);
        p.real(p.uniforms[0],static_cast<float>(p.view.phase));p.real(p.uniforms[1],static_cast<float>(p.view.windStrength));
        p.real(p.uniforms[2],glIsEnabled(GL_LIGHT0) ? 1.f : 0.f);p.real(p.uniforms[3],glIsEnabled(GL_LIGHT1) ? 1.f : 0.f);p.real(p.uniforms[4],static_cast<float>(wire));
        GLint fog=0;glGetIntegerv(GL_FOG_MODE,&fog);p.real(p.uniforms[5],!glIsEnabled(GL_FOG) ? 0.f : fog==GL_EXP ? 1.f : 2.f);
        p.real(p.uniforms[6],p.view.enabled ? 1.f : 0.f);
        glCallList(list);p.use(static_cast<GLuint>(previous));
    } else if(!p.deformed.empty()) {
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_NORMAL_ARRAY);
        if(wire==0) glEnableClientState(GL_COLOR_ARRAY);else glDisableClientState(GL_COLOR_ARRAY);
        const auto* bytes=reinterpret_cast<const unsigned char*>(p.deformed.data());
        glVertexPointer(3,GL_DOUBLE,sizeof(FoliageVertex),bytes+offsetof(FoliageVertex,position));
        glNormalPointer(GL_DOUBLE,sizeof(FoliageVertex),bytes+offsetof(FoliageVertex,normal));
        glColorPointer(3,GL_FLOAT,sizeof(FoliageVertex),bytes+offsetof(FoliageVertex,color));
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(p.deformed.size()));glPopClientAttrib();
    }
    glPopAttrib();
}
} // namespace planet

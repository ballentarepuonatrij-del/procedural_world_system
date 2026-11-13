#include "planet/win32_materials.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <cstring>

namespace planet {
namespace {
template<class T> T entry(const char* name) {
    const auto p=wglGetProcAddress(name);const auto address=reinterpret_cast<std::uintptr_t>(p);
    if(address<=3 || address==~std::uintptr_t{}) return nullptr;
    T result{};static_assert(sizeof(result)==sizeof(p));std::memcpy(&result,&p,sizeof(result));return result;
}
constexpr const char* vertexShader=R"GLSL(#version 120
uniform mat3 worldBasis,waterBasis;
uniform vec3 origin,waterOrigin;
uniform vec4 waterGrid;
uniform float radius,sunEnabled,lampEnabled;
varying vec3 texPosition,worldNormal,eyePosition,eyeNormal,illumination;
varying vec4 weights;
varying vec2 waterUV;
varying float backgroundWetness;
void main() {
    // Match fixed-function shadow volumes exactly, including depth rounding.
    gl_Position=ftransform();
    eyePosition=(gl_ModelViewMatrix*gl_Vertex).xyz;
    eyeNormal=normalize(gl_NormalMatrix*gl_Normal);
    illumination=gl_LightModel.ambient.rgb+gl_LightSource[0].diffuse.rgb*sunEnabled*
        max(dot(eyeNormal,normalize(gl_LightSource[0].position.xyz)),0.0);
    if(lampEnabled>0.5) {
        vec3 delta=gl_LightSource[1].position.xyz-eyePosition;
        float d=length(delta);
        float falloff=1.0/(gl_LightSource[1].constantAttenuation+gl_LightSource[1].linearAttenuation*d+gl_LightSource[1].quadraticAttenuation*d*d);
        illumination+=gl_LightSource[1].diffuse.rgb*max(dot(eyeNormal,normalize(delta)),0.0)*falloff;
    }
    worldNormal=worldBasis*gl_Normal;
    texPosition=(worldBasis*gl_Vertex.xyz+origin)/8.0;
    vec3 hyd=waterBasis*gl_Vertex.xyz+waterOrigin;
    waterUV=((hyd.xy/max(hyd.z,1.0)*radius-waterGrid.xy)*waterGrid.z+0.5)/waterGrid.w;
    weights=gl_MultiTexCoord1;
    backgroundWetness=gl_MultiTexCoord2.x;
    gl_FrontColor=gl_Color;
}
)GLSL";
constexpr const char* fragmentShader=R"GLSL(#version 120
uniform sampler2D patterns,moisture;
uniform float sunEnabled,lampEnabled,fogMode,waterEnabled,detailStrength;
varying vec3 texPosition,worldNormal,eyePosition,eyeNormal,illumination;
varying vec4 weights;
varying vec2 waterUV;
varying float backgroundWetness;
void main() {
    vec4 w=max(weights,vec4(0.0));w/=max(dot(w,vec4(1.0)),0.001);
    float distanceToEye=length(eyePosition);
    float detail=0.0;
    // Uniform branch: no microtexture work in very fast flight. Keep diffuse
    // illumination on the vertices, as in the original renderer, to bound cost.
    if(detailStrength>0.001) {
        vec3 axis=abs(worldNormal);axis*=axis;axis*=axis;
        axis/=max(axis.x+axis.y+axis.z,0.001);
        vec4 grain=texture2D(patterns,texPosition.yz)*axis.x+
                   texture2D(patterns,texPosition.zx)*axis.y+
                   texture2D(patterns,texPosition.xy)*axis.z;
        float height=dot(grain,w)-0.5;
        float nearDetail=(1.0-smoothstep(80.0,240.0,distanceToEye))*detailStrength;
        detail=height*nearDetail;
    }
    float wetness=backgroundWetness;
    if(waterEnabled>0.5 && waterUV.x>0.0 && waterUV.y>0.0 && waterUV.x<1.0 && waterUV.y<1.0) {
        vec4 wet=texture2D(moisture,waterUV);
        vec2 border=min(waterUV*64.0-0.5,63.5-waterUV*64.0);
        wetness=mix(backgroundWetness,wet.r,wet.a*smoothstep(0.0,4.0,min(border.x,border.y)));
    }
    float dark=1.0-wetness*dot(w,vec4(0.28,0.42,0.34,0.08));
    vec3 albedo=gl_Color.rgb*dark*(1.0+detail*1.5);
    vec3 color=albedo*illumination;
    if(wetness>0.001 && sunEnabled>0.5) {
        vec3 n=normalize(eyeNormal),l=normalize(gl_LightSource[0].position.xyz);
        float ndl=max(dot(n,l),0.0);
        float roughness=mix(dot(w,vec4(0.82,0.96,0.91,0.78)),0.24+0.4*w.w,wetness);
        vec3 viewDirection=-eyePosition/max(distanceToEye,0.001);
        vec3 halfVector=normalize(l+viewDirection);
        float fresnel=0.025+0.22*pow(1.0-max(dot(n,viewDirection),0.0),5.0);
        float spec=pow(max(dot(n,halfVector),0.0),mix(10.0,100.0,1.0-roughness))*fresnel*wetness*ndl;
        color+=gl_LightSource[0].diffuse.rgb*spec;
    }
    float fogDistance=abs(eyePosition.z)*gl_Fog.density;
    float fog=fogMode<0.5 ? 1.0 : exp(fogMode<1.5 ? -fogDistance : -fogDistance*fogDistance);
    gl_FragColor=vec4(mix(gl_Fog.color.rgb,color,clamp(fog,0.0,1.0)),1.0);
}
)GLSL";
}
struct TerrainMaterials::Impl {
    using CreateShader=GLuint(APIENTRY*)(GLenum);using ShaderSource=void(APIENTRY*)(GLuint,GLsizei,const char* const*,const GLint*);
    using Action=void(APIENTRY*)(GLuint);using GetStatus=void(APIENTRY*)(GLuint,GLenum,GLint*);
    using CreateProgram=GLuint(APIENTRY*)();using Attach=void(APIENTRY*)(GLuint,GLuint);
    using Location=GLint(APIENTRY*)(GLuint,const char*);using Integer=void(APIENTRY*)(GLint,GLint);using Real=void(APIENTRY*)(GLint,GLfloat);
    using Vector3=void(APIENTRY*)(GLint,GLfloat,GLfloat,GLfloat);using Vector4=void(APIENTRY*)(GLint,GLfloat,GLfloat,GLfloat,GLfloat);
    using Matrix=void(APIENTRY*)(GLint,GLsizei,GLboolean,const GLfloat*);using Active=void(APIENTRY*)(GLenum);
    using Coord=void(APIENTRY*)(GLenum,GLfloat,GLfloat,GLfloat,GLfloat);
    CreateShader createShader=entry<CreateShader>("glCreateShader");ShaderSource shaderSource=entry<ShaderSource>("glShaderSource");
    Action compile=entry<Action>("glCompileShader"),deleteShader=entry<Action>("glDeleteShader"),link=entry<Action>("glLinkProgram"),use=entry<Action>("glUseProgram"),deleteProgram=entry<Action>("glDeleteProgram");
    GetStatus shaderStatus=entry<GetStatus>("glGetShaderiv"),programStatus=entry<GetStatus>("glGetProgramiv");
    CreateProgram createProgram=entry<CreateProgram>("glCreateProgram");Attach attach=entry<Attach>("glAttachShader");
    Location location=entry<Location>("glGetUniformLocation");Integer integer=entry<Integer>("glUniform1i");Real real=entry<Real>("glUniform1f");
    Vector3 vector3=entry<Vector3>("glUniform3f");Vector4 vector4=entry<Vector4>("glUniform4f");Matrix matrix=entry<Matrix>("glUniformMatrix3fv");
    Active active=entry<Active>("glActiveTexture");Coord coord=entry<Coord>("glMultiTexCoord4f");
    GLuint program=0,texture=0,wetTexture=0;GLint previousProgram=0,previousActive=0,previousWetTexture=0,previousTexture=0;
    bool bound=false,waterEnabled=false;
    bool waterDirty=true;
    std::uint64_t wetSteps=0,wetRevision=0,wetSeed=0;
    Vec3 wetAnchor{};double wetX=0,wetZ=0;
    std::array<GLint,11> uniforms{};
    ~Impl() {if(program) deleteProgram(program);if(texture) glDeleteTextures(1,&texture);if(wetTexture) glDeleteTextures(1,&wetTexture);}
    void setup() {
        if(!createShader || !shaderSource || !compile || !deleteShader || !link || !use || !deleteProgram || !shaderStatus || !programStatus || !createProgram || !attach || !location || !integer || !real || !vector3 || !vector4 || !matrix || !active || !coord) return;
        const auto shader=[&](GLenum kind,const char* source) {
            const auto id=createShader(kind);shaderSource(id,1,&source,nullptr);compile(id);GLint ok=0;shaderStatus(id,0x8B81,&ok);
            if(!ok) {deleteShader(id);return GLuint{};}return id;
        };
        const auto vertex=shader(0x8B31,vertexShader),fragment=shader(0x8B30,fragmentShader);
        if(vertex && fragment) {program=createProgram();attach(program,vertex);attach(program,fragment);link(program);GLint ok=0;programStatus(program,0x8B82,&ok);if(!ok) {deleteProgram(program);program=0;}}
        if(vertex) deleteShader(vertex);
        if(fragment) deleteShader(fragment);
        if(!program) return;
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);use(program);
        integer(location(program,"patterns"),0);integer(location(program,"moisture"),1);
        constexpr std::array names{"worldBasis","waterBasis","origin","waterOrigin","waterGrid","radius","sunEnabled","lampEnabled","fogMode","waterEnabled","detailStrength"};
        for(std::size_t i=0;i<names.size();++i) uniforms[i]=location(program,names[i]);
        use(static_cast<GLuint>(previous));
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        int level=0;for(const auto& mip:makeMaterialTexture()) glTexImage2D(GL_TEXTURE_2D,level++,GL_RGBA,mip.size,mip.size,0,GL_RGBA,GL_UNSIGNED_BYTE,mip.rgba.data());
        glGenTextures(1,&wetTexture);glBindTexture(GL_TEXTURE_2D,wetTexture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE_ALPHA,64,64,0,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,nullptr);
    }
};
TerrainMaterials::TerrainMaterials()=default;
TerrainMaterials::~TerrainMaterials()=default;
void TerrainMaterials::initialize(bool enabled) {if(enabled) {impl_=std::make_unique<Impl>();impl_->setup();}}
void TerrainMaterials::invalidate() {if(impl_) impl_->waterDirty=true;}
const char* TerrainMaterials::backend() const {return impl_ && impl_->program ? "triplanar-glsl" : "fixed-function";}
void TerrainMaterials::vertex(const TerrainVertex& v) const {
    const bool shader=impl_ && impl_->program;
    const auto color=shader ? v.color : wetMaterialColor(v.color,v.material);
    glColor3f(color.r,color.g,color.b);glNormal3d(v.normal.x,v.normal.y,v.normal.z);
    glTexCoord2d(v.u,v.v);
    if(shader) {const auto& w=v.material.weights;impl_->coord(0x84C1,w[0],w[1],w[2],w[3]);impl_->coord(0x84C2,v.material.backgroundWetness,0,0,0);}
    glVertex3d(v.position.x,v.position.y,v.position.z);
}
void TerrainMaterials::prepare(const World& world,const Hydrology& water,Vec3 anchor,Vec3 eastAxis,Vec3 northAxis,double detailStrength) {
    if(!impl_ || !impl_->program) return;
    auto& p=*impl_;GLint previous=0;glGetIntegerv(0x8B8D,&previous);p.use(p.program);
    p.real(p.uniforms[10],static_cast<float>(detailStrength));
    const auto setVector=[&](GLint id,Vec3 v){p.vector3(id,static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z));};
    const auto setMatrix=[&](GLint id,Vec3 a,Vec3 b,Vec3 c) {
        const GLfloat values[]{static_cast<float>(a.x),static_cast<float>(a.y),static_cast<float>(a.z),static_cast<float>(b.x),static_cast<float>(b.y),static_cast<float>(b.z),static_cast<float>(c.x),static_cast<float>(c.y),static_cast<float>(c.z)};
        p.matrix(id,1,GL_FALSE,values);
    };
    setMatrix(p.uniforms[0],eastAxis,anchor,northAxis);setVector(p.uniforms[2],materialOrigin(anchor*world.config().radius));
    const Vec3 hyd=water.directionAt(0,0),he=east(hyd),hn=north(hyd);
    const auto project=[&](Vec3 v){return Vec3{dot(v,he),dot(v,hn),dot(v,hyd)};};
    setMatrix(p.uniforms[1],project(eastAxis),project(anchor),project(northAxis));
    setVector(p.uniforms[3],project(anchor*world.config().radius));
    p.vector4(p.uniforms[4],static_cast<float>(water.cellX(0)),static_cast<float>(water.cellZ(0)),1.f/4,64);
    p.real(p.uniforms[5],static_cast<float>(world.config().radius));
    p.waterDirty=p.waterDirty || p.waterEnabled!=water.enabled();p.waterEnabled=water.enabled();
    if(p.waterEnabled && (p.waterDirty || p.wetSteps!=water.steps() || p.wetRevision!=water.revision() ||
       p.wetSeed!=world.config().seed || length(p.wetAnchor-hyd)>1e-12 || p.wetX!=water.cellX(0) || p.wetZ!=water.cellZ(0))) {
        std::array<unsigned char,64*64*2> pixels{};const auto& cells=water.grid().cells;
        for(std::size_t i=0;i<cells.size();++i) {
            const auto& cell=cells[i];const double saturation=cell.soil>0 ? cell.soilWater/(RunoffGrid::porosity*cell.soil) : 0;
            pixels[i*2]=static_cast<unsigned char>(std::lround(255*(cell.sea || cell.reservoir ? 1 : materialWetness(saturation,cell.water))));
            pixels[i*2+1]=cell.masked ? 0 : 255;
        }
        GLint binding=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);glBindTexture(GL_TEXTURE_2D,p.wetTexture);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,64,64,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,pixels.data());glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(binding));
        p.waterDirty=false;p.wetSteps=water.steps();p.wetRevision=water.revision();p.wetSeed=world.config().seed;
        p.wetAnchor=hyd;p.wetX=water.cellX(0);p.wetZ=water.cellZ(0);
    }
    p.use(static_cast<GLuint>(previous));
}
void TerrainMaterials::begin(bool enabled) {
    if(!enabled || !impl_ || !impl_->program) return;
    auto& p=*impl_;glGetIntegerv(0x8B8D,&p.previousProgram);glGetIntegerv(0x84E0,&p.previousActive);
    p.active(0x84C0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&p.previousTexture);glBindTexture(GL_TEXTURE_2D,p.texture);
    p.active(0x84C1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&p.previousWetTexture);glBindTexture(GL_TEXTURE_2D,p.wetTexture);p.active(0x84C0);
    p.use(p.program);p.real(p.uniforms[6],glIsEnabled(GL_LIGHT0) ? 1.f : 0.f);p.real(p.uniforms[7],glIsEnabled(GL_LIGHT1) ? 1.f : 0.f);
    GLint fog=0;glGetIntegerv(GL_FOG_MODE,&fog);p.real(p.uniforms[8],!glIsEnabled(GL_FOG) ? 0.f : fog==GL_EXP ? 1.f : 2.f);
    p.real(p.uniforms[9],p.waterEnabled ? 1.f : 0.f);p.bound=true;
}
void TerrainMaterials::end() {
    if(!impl_ || !impl_->bound) return;
    auto& p=*impl_;p.use(static_cast<GLuint>(p.previousProgram));p.active(0x84C1);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(p.previousWetTexture));
    p.active(0x84C0);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(p.previousTexture));p.active(static_cast<GLenum>(p.previousActive));p.bound=false;
}
} // namespace planet

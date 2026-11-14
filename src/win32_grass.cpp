#include "planet/win32_grass.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <cstring>

namespace planet {
namespace {
template<class T> T entry(const char* name) {
    const auto p=wglGetProcAddress(name);const auto a=reinterpret_cast<std::uintptr_t>(p);if(a<=3 || a==~std::uintptr_t{}) return nullptr;
    T out{};static_assert(sizeof(out)==sizeof(p));std::memcpy(&out,&p,sizeof(out));return out;
}
constexpr const char* vertexSource=R"GLSL(#version 120
uniform sampler2D contacts;
uniform mat3 toField,fromField;
uniform vec3 fieldOrigin,eye,windShort,windTall;
uniform vec4 grid;
uniform float phase,visibility,sunEnabled,lampEnabled,wire;
varying float fogDepth;
void main() {
    vec3 root=gl_MultiTexCoord0.xyz;
    float flex=gl_MultiTexCoord0.w,height=gl_MultiTexCoord1.x,tall=gl_MultiTexCoord1.z;
    float range=length(root-eye);
    vec3 rootView=(gl_ModelViewMatrix*vec4(root,1.0)).xyz;
    // Conservative sphere covers static curl plus any allowed deflection.
    // Hidden blades skip texture reads, gusts and lighting in both passes.
    float bound=2.0*height+0.5;
    if(range>mix(38.0,44.0,tall) || rootView.z>bound ||
       abs(rootView.x)*gl_ProjectionMatrix[0][0]>-rootView.z+bound*(1.0+gl_ProjectionMatrix[0][0]) ||
       abs(rootView.y)*gl_ProjectionMatrix[1][1]>-rootView.z+bound*(1.0+gl_ProjectionMatrix[1][1])) {
        gl_Position=vec4(2.0,2.0,2.0,1.0);fogDepth=0.0;
        gl_FrontColor=vec4(0.0);gl_BackColor=vec4(0.0);return;
    }
    vec3 fp=toField*root+fieldOrigin;
    vec2 xy=(fp.xz-grid.xy)*grid.z;
    vec3 bend=vec3(0.0);
    if(xy.x>=0.0 && xy.y>=0.0 && xy.x<63.0 && xy.y<63.0) {
        vec2 field=texture2D(contacts,(xy+0.5)/grid.w).ra*2.0-1.0;
        float edge=smoothstep(0.0,2.0,min(min(xy.x,xy.y),min(63.0-xy.x,63.0-xy.y)));
        bend=fromField*vec3(field.x,0.0,field.y)*edge;
    }
    float gust=0.72+0.22*sin(gl_MultiTexCoord1.y+phase)+0.06*sin(gl_MultiTexCoord1.y*2.0+phase*2.0);
    bend+=mix(windShort,windTall,tall)*gust;
    bend.y=0.0;
    float amount=length(bend);if(amount>0.97) bend*=0.97/amount;
    float drop=(1.0-sqrt(max(0.0,1.0-dot(bend,bend))))*height;
    vec3 position=gl_Vertex.xyz+vec3(bend.x*height,-drop,bend.z*height)*flex;
    float fade=(1.0-smoothstep(mix(24.0,32.0,tall),mix(38.0,44.0,tall),range))*visibility;
    position=root+(position-root)*fade;
    vec4 eyePosition=gl_ModelViewMatrix*vec4(position,1.0);
    gl_Position=gl_ProjectionMatrix*eyePosition;fogDepth=abs(eyePosition.z);
    vec3 normal=gl_Normal;normal.y-=dot(normal.xz,bend.xz)*2.0*flex;
    normal=normalize(gl_NormalMatrix*normal);
    vec3 light=gl_LightModel.ambient.rgb+gl_LightSource[0].diffuse.rgb*sunEnabled*(0.3+0.7*abs(dot(normal,normalize(gl_LightSource[0].position.xyz))));
    if(lampEnabled>0.5) {
        vec3 delta=gl_LightSource[1].position.xyz-eyePosition.xyz;float d=length(delta);
        light+=gl_LightSource[1].diffuse.rgb*abs(dot(normal,normalize(delta)))/(1.0+0.02*d+0.0015*d*d);
    }
    gl_FrontColor=vec4(mix(gl_Color.rgb*light,vec3(0.12,0.64,0.78),wire),1.0);
    gl_BackColor=gl_FrontColor;
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
struct GrassRenderer::Impl {
    using CreateShader=GLuint(APIENTRY*)(GLenum);using Source=void(APIENTRY*)(GLuint,GLsizei,const char* const*,const GLint*);
    using Action=void(APIENTRY*)(GLuint);using Status=void(APIENTRY*)(GLuint,GLenum,GLint*);using CreateProgram=GLuint(APIENTRY*)();using Attach=void(APIENTRY*)(GLuint,GLuint);
    using Location=GLint(APIENTRY*)(GLuint,const char*);using Real=void(APIENTRY*)(GLint,GLfloat);using Integer=void(APIENTRY*)(GLint,GLint);
    using Vector3=void(APIENTRY*)(GLint,GLfloat,GLfloat,GLfloat);using Vector4=void(APIENTRY*)(GLint,GLfloat,GLfloat,GLfloat,GLfloat);
    using Matrix=void(APIENTRY*)(GLint,GLsizei,GLboolean,const GLfloat*);using Coord=void(APIENTRY*)(GLenum,GLfloat,GLfloat,GLfloat,GLfloat);
    CreateShader createShader=entry<CreateShader>("glCreateShader");Source source=entry<Source>("glShaderSource");
    Action compile=entry<Action>("glCompileShader"),deleteShader=entry<Action>("glDeleteShader"),link=entry<Action>("glLinkProgram"),use=entry<Action>("glUseProgram"),deleteProgram=entry<Action>("glDeleteProgram");
    Status shaderStatus=entry<Status>("glGetShaderiv"),programStatus=entry<Status>("glGetProgramiv");CreateProgram createProgram=entry<CreateProgram>("glCreateProgram");Attach attach=entry<Attach>("glAttachShader");
    Location location=entry<Location>("glGetUniformLocation");Real real=entry<Real>("glUniform1f");Integer integer=entry<Integer>("glUniform1i");
    Vector3 vector3=entry<Vector3>("glUniform3f");Vector4 vector4=entry<Vector4>("glUniform4f");Matrix matrix=entry<Matrix>("glUniformMatrix3fv");Coord coord=entry<Coord>("glMultiTexCoord4f");
    GLuint program=0,texture=0;std::uint64_t uploaded=~std::uint64_t{};
    std::array<GLint,13> uniforms{};
    GrassMesh mesh;std::size_t drawn=0,geometryVertices=0;
    ~Impl() {if(program) deleteProgram(program);if(texture) glDeleteTextures(1,&texture);}
    void setup() {
        if(!createShader || !source || !compile || !deleteShader || !link || !use || !deleteProgram || !shaderStatus || !programStatus || !createProgram || !attach || !location || !real || !integer || !vector3 || !vector4 || !matrix || !coord) return;
        GLint units=0;glGetIntegerv(0x8B4C,&units);if(units<1) return;
        const auto shader=[&](GLenum type,const char* text) {const auto id=createShader(type);source(id,1,&text,nullptr);compile(id);GLint ok=0;shaderStatus(id,0x8B81,&ok);if(!ok) {deleteShader(id);return GLuint{};}return id;};
        const auto vertex=shader(0x8B31,vertexSource),fragment=shader(0x8B30,fragmentSource);
        if(vertex && fragment) {program=createProgram();attach(program,vertex);attach(program,fragment);link(program);GLint ok=0;programStatus(program,0x8B82,&ok);if(!ok) {deleteProgram(program);program=0;}}
        if(vertex) deleteShader(vertex);
        if(fragment) deleteShader(fragment);
        if(!program) return;
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);use(program);integer(location(program,"contacts"),0);
        constexpr std::array names{"toField","fromField","fieldOrigin","eye","windShort","windTall","grid","phase","visibility","sunEnabled","lampEnabled","wire","fogMode"};
        for(std::size_t i=0;i<names.size();++i) uniforms[i]=location(program,names[i]);
        use(static_cast<GLuint>(previous));
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE16_ALPHA16,64,64,0,GL_LUMINANCE_ALPHA,GL_UNSIGNED_SHORT,nullptr);
    }
};
GrassRenderer::GrassRenderer()=default;
GrassRenderer::~GrassRenderer()=default;
void GrassRenderer::initialize(bool shaders) {impl_=std::make_unique<Impl>();if(shaders) impl_->setup();}
const char* GrassRenderer::backend() const {return impl_ && impl_->program ? "vertex-spring-field" : "cpu-bounded";}
std::size_t GrassRenderer::shortCount() const {return impl_ ? impl_->mesh.shortCount : 0;}
std::size_t GrassRenderer::tallCount() const {return impl_ ? impl_->mesh.tallCount : 0;}
std::size_t GrassRenderer::vertices() const {return impl_ ? impl_->drawn : 0;}
void GrassRenderer::adopt(GrassMesh mesh) {
    auto& p=*impl_;p.geometryVertices=mesh.vertices.size();
    if(p.program) {p.mesh.shortCount=mesh.shortCount;p.mesh.tallCount=mesh.tallCount;}
    else p.mesh=std::move(mesh); // Only the compatibility path needs CPU geometry after upload.
}
void GrassRenderer::vertex(const GrassVertex& v) const {
    glColor3f(v.color.r,v.color.g,v.color.b);glNormal3d(v.normal.x,v.normal.y,v.normal.z);
    if(impl_->program) {
        impl_->coord(0x84C0,static_cast<float>(v.root.x),static_cast<float>(v.root.y),static_cast<float>(v.root.z),v.flex);
        impl_->coord(0x84C1,v.height,v.phase,v.tall,0);
    }
    glVertex3d(v.position.x,v.position.y,v.position.z);
}
void GrassRenderer::draw(unsigned list,const GrassField& field,const GrassDraw& view) {
    auto& p=*impl_;p.drawn=0;if(view.visibility<=0 || p.geometryVertices==0) return;
    glPushAttrib(GL_ENABLE_BIT|GL_LIGHTING_BIT|GL_POLYGON_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
    glDisable(GL_CULL_FACE);glDisable(GL_TEXTURE_2D);glEnable(GL_COLOR_MATERIAL);glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE);
    if(view.wireframe) {glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glDisable(GL_LIGHTING);}
    const auto local=[&](Vec3 v){return Vec3{dot(v,view.east),dot(v,view.up),dot(v,view.north)};};
    if(p.program) {
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);p.use(p.program);glBindTexture(GL_TEXTURE_2D,p.texture);
        if(p.uploaded!=field.revision()) {const auto pixels=field.texture();glTexSubImage2D(GL_TEXTURE_2D,0,0,0,64,64,GL_LUMINANCE_ALPHA,GL_UNSIGNED_SHORT,pixels.data());p.uploaded=field.revision();}
        const auto vec=[&](int id,Vec3 v) {p.vector3(p.uniforms[static_cast<std::size_t>(id)],static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z));};
        const auto matrix=[&](int id,Vec3 a,Vec3 b,Vec3 c) {
            const GLfloat values[]{static_cast<float>(a.x),static_cast<float>(a.y),static_cast<float>(a.z),static_cast<float>(b.x),static_cast<float>(b.y),static_cast<float>(b.z),static_cast<float>(c.x),static_cast<float>(c.y),static_cast<float>(c.z)};
            p.matrix(p.uniforms[static_cast<std::size_t>(id)],1,GL_FALSE,values);
        };
        const auto toField=[&](Vec3 v) {return Vec3{dot(v,field.eastAxis()),dot(v,field.anchor()),dot(v,field.northAxis())};};
        matrix(0,toField(view.east),toField(view.up),toField(view.north));matrix(1,local(field.eastAxis()),local(field.anchor()),local(field.northAxis()));
        vec(2,field.local(view.origin));vec(3,view.eye);vec(4,view.windShort);vec(5,view.windTall);
        p.vector4(p.uniforms[6],static_cast<float>(field.originX()),static_cast<float>(field.originZ()),1,64);
        p.real(p.uniforms[7],static_cast<float>(view.phase));p.real(p.uniforms[8],static_cast<float>(view.visibility));
        p.real(p.uniforms[9],glIsEnabled(GL_LIGHT0) ? 1.f : 0.f);p.real(p.uniforms[10],glIsEnabled(GL_LIGHT1) ? 1.f : 0.f);p.real(p.uniforms[11],view.wireframe ? 1.f : 0.f);
        GLint fog=0;glGetIntegerv(GL_FOG_MODE,&fog);p.real(p.uniforms[12],!glIsEnabled(GL_FOG) ? 0.f : fog==GL_EXP ? 1.f : 2.f);
        glCallList(list);p.drawn=p.geometryVertices;p.use(static_cast<GLuint>(previous));
    } else {
        // Compatibility path: at most 12000 vertices, nearby clumps only.
        glBegin(GL_TRIANGLES);
        for(const auto& clump:p.mesh.clumps) {
            const double range=length(clump.base-view.eye);if(range>16 || p.drawn+clump.count>12000) continue;
            const double fade=(1-smoothstep(10,16,range))*view.visibility;
            const auto world=view.origin+view.east*clump.base.x+view.up*clump.base.y+view.north*clump.base.z;
            const auto contact=field.sample(world);const auto bend=local(field.eastAxis()*contact.x+field.northAxis()*contact.z);
            for(std::size_t i=clump.first;i<clump.first+clump.count;++i) {
                const auto& v=p.mesh.vertices[i];const double gust=.72+.22*std::sin(v.phase+view.phase)+.06*std::sin(2*v.phase+2*view.phase);
                const auto point=bendGrassPosition(v,bend+(clump.tall ? view.windTall : view.windShort)*gust,fade);
                const auto color=view.wireframe ? Color{.12f,.64f,.78f} : v.color;
                glColor3f(color.r,color.g,color.b);glNormal3d(v.normal.x,v.normal.y,v.normal.z);glVertex3d(point.x,point.y,point.z);++p.drawn;
            }
        }
        glEnd();
    }
    glPopAttrib();
}
} // namespace planet

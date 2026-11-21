#include "planet/win32_foliage.hpp"
#include "planet/water_view.hpp"
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
uniform float waveSin,waveCos,sunEnabled,lampEnabled,wire,motion;
varying float fogDepth;
void main() {
    vec3 position=gl_Vertex.xyz,normal=gl_Normal;
    vec2 uv=gl_MultiTexCoord0.xy;
    gl_TexCoord[0]=gl_MultiTexCoord0;
    float flex=gl_MultiTexCoord1.x,height=gl_MultiTexCoord1.y,slot=gl_MultiTexCoord1.w;
    if(slot>=0.0 && motion>0.5) {
        vec3 bend=(texture2D(bends,vec2((slot+0.5)/256.0,0.5)).rgb*65535.0-32768.0)/32767.0;
        position+=(bend-vec3(0.0,0.5*dot(bend,bend),0.0))*height*flex;
        position+=vec3(0.3,1.0,0.2)*dot(gl_MultiTexCoord2.xy,vec2(waveSin,waveCos));
        normal.y-=dot(normal,bend)*2.0*flex;
    }
    vec4 eye=gl_ModelViewMatrix*vec4(position,1.0);
    gl_Position=gl_ProjectionMatrix*eye;fogDepth=abs(eye.z);
    normal=normalize(gl_NormalMatrix*normal);
    vec3 ambient=gl_LightModel.ambient.rgb;
    float diffuse=dot(normal,normalize(gl_LightSource[0].position.xyz));
    vec3 front=ambient+gl_LightSource[0].diffuse.rgb*sunEnabled*max(0.0,diffuse);
    vec3 back=ambient+gl_LightSource[0].diffuse.rgb*sunEnabled*max(0.0,-diffuse);
    if(uv.x<0.5 || uv.y<0.5) {
        vec3 transmitted=0.40*sunEnabled*gl_LightSource[0].diffuse.rgb;
        front+=transmitted*max(0.0,-diffuse);
        back+=transmitted*max(0.0,diffuse);
    }
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
uniform sampler2D leaves;
varying float fogDepth;
void main() {
    float d=fogDepth*gl_Fog.density;
    float fog=fogMode<0.5 ? 1.0 : exp(fogMode<1.5 ? -d : -d*d);
    vec4 leaf=texture2D(leaves,gl_TexCoord[0].xy);
    gl_FragColor=vec4(mix(gl_Fog.color.rgb,gl_Color.rgb*leaf.rgb,clamp(fog,0.0,1.0)),leaf.a);
}
)GLSL";
constexpr unsigned leafCutoff=87;
void preserveLeafCoverage(std::vector<LeafPixel>& pixels,int size,const std::array<double,3>& coverage) {
    // At coarser levels a simple average erases small needles at alpha test.
    // Keep the covered fraction of each species tile while it spans several texels.
    const int tile=size/2;
    for(int i=0;i<3;++i) {
        const auto pixel=[&](int x,int y)->LeafPixel& {return pixels[(y+(i/2)*tile)*size+x+(i%2)*tile];};
        std::array<int,256> histogram{};
        for(int y=0;y<tile;++y) for(int x=0;x<tile;++x) ++histogram[pixel(x,y)[3]];
        const auto target=static_cast<int>(std::lround(coverage[i]*tile*tile));
        int value=255,count=0;
        while(value>0 && count+histogram[value]<target) count+=histogram[value--];
        if(value==0 || value>static_cast<int>(leafCutoff)) continue;
        const double gain=std::min(1.6,(leafCutoff+1.)/value);
        for(int y=0;y<tile;++y) for(int x=0;x<tile;++x) {
            auto& alpha=pixel(x,y)[3];alpha=static_cast<unsigned char>(std::min(255.,std::round(alpha*gain)));
        }
    }
}
// Build the filtered levels once. Transparent texels contribute to coverage,
// while only covered texels contribute to leaf colour along the edges.
GLuint uploadLeaves() {
    GLuint name=0;
    glGenTextures(1,&name);
    glPushAttrib(GL_TEXTURE_BIT);
    glBindTexture(GL_TEXTURE_2D,name);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
    auto pixels=makeLeafAtlas();
    std::array<double,3> coverage{};
    for(int i=0;i<3;++i) {
        int solid=0;
        for(int y=0;y<leafTileSize;++y) for(int x=0;x<leafTileSize;++x)
            solid+=pixels[(y+(i/2)*leafTileSize)*leafAtlasSize+x+(i%2)*leafTileSize][3]>leafCutoff;
        coverage[i]=static_cast<double>(solid)/(leafTileSize*leafTileSize);
    }
    int size=leafAtlasSize;
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    for(int level=0;;) {
        if(size<=limit) glTexImage2D(GL_TEXTURE_2D,level++,GL_RGBA,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if(size==1) break;
        const int small=size/2;
        for(int y=0;y<small;++y) for(int x=0;x<small;++x) {
            std::array<unsigned,3> weighted{};unsigned alpha=0;
            for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
                const auto& p=pixels[static_cast<std::size_t>((2*y+j)*size+2*x+i)];
                alpha+=p[3];
                for(unsigned c=0;c<3;++c) weighted[c]+=static_cast<unsigned>(p[c])*p[3];
            }
            auto& result=pixels[static_cast<std::size_t>(y*small+x)];
            for(unsigned c=0;c<3;++c) result[c]=alpha ? static_cast<unsigned char>(weighted[c]/alpha) : 220;
            result[3]=static_cast<unsigned char>((alpha+2)/4);
        }
        if(small>=8) preserveLeafCoverage(pixels,small,coverage);
        size=small;
    }
    glPopAttrib();
    return name;
}

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
    using Activate=void(APIENTRY*)(GLenum);
    Activate activate=entry<Activate>("glActiveTexture");
    GLuint atlas=0,program=0,texture=0;std::array<GLint,7> uniforms{};
    std::vector<FoliageVertex> vertices,deformed;std::vector<FoliageRange> ranges;
    struct Lists {
        GLuint first=0;
        std::vector<FoliageBatch> batches;
        std::vector<GLuint> visible;
        std::size_t select(const ViewFrustum& frustum,double bend,double wind) {
            visible.clear();std::size_t vertices=0;
            for(std::size_t i=0;i<batches.size();++i) {
                const auto& b=batches[i];
                if(frustum.outside(b.center,b.sweptRadius(bend,wind))) continue;
                visible.push_back(first+static_cast<GLuint>(i));vertices+=b.count;
            }
            return vertices;
        }
        void draw() const {for(auto id:visible) glCallList(id);}
    } fixed,moving;
    FoliageDraw view;std::size_t leaves=0,animated=0;
    ~Impl() {if(program) deleteProgram(program);if(texture) glDeleteTextures(1,&texture);if(atlas) glDeleteTextures(1,&atlas);}
    void setup() {
        if(!createShader || !source || !compile || !deleteShader || !link || !use || !deleteProgram || !shaderStatus || !programStatus || !createProgram || !attach || !location || !real || !integer || !coord || !activate) return;
        GLint units=0;glGetIntegerv(0x8B4C,&units);if(units<1) return;
        const auto shader=[&](GLenum type,const char* text) {const auto id=createShader(type);source(id,1,&text,nullptr);compile(id);GLint ok=0;shaderStatus(id,0x8B81,&ok);if(!ok) {deleteShader(id);return GLuint{};}return id;};
        const auto vertex=shader(0x8B31,vertexSource),fragment=shader(0x8B30,fragmentSource);
        if(vertex && fragment) {program=createProgram();attach(program,vertex);attach(program,fragment);link(program);GLint ok=0;programStatus(program,0x8B82,&ok);if(!ok) {deleteProgram(program);program=0;}}
        if(vertex) deleteShader(vertex);
        if(fragment) deleteShader(fragment);
        if(!program) return;
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);use(program);integer(location(program,"bends"),1);integer(location(program,"leaves"),0);
        constexpr std::array names{"waveSin","waveCos","sunEnabled","lampEnabled","wire","fogMode","motion"};
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
void FoliageRenderer::initialize(bool shaders) {impl_=std::make_unique<Impl>();impl_->atlas=uploadLeaves();if(shaders) impl_->setup();}
const char* FoliageRenderer::backend() const {return impl_ && impl_->program ? "vertex-plant-springs" : "cpu-bounded";}
std::size_t FoliageRenderer::leafVertices() const {return impl_ ? impl_->leaves : 0;}
std::size_t FoliageRenderer::animatedVertices() const {return impl_ ? impl_->animated : 0;}
void FoliageRenderer::adopt(PlantMesh& mesh,unsigned staticFirst,unsigned movingFirst) {
    auto& p=*impl_;p.leaves=mesh.leafVertices;
    p.fixed={staticFirst,std::move(mesh.staticBatches),{}};
    p.moving={movingFirst,std::move(mesh.movingBatches),{}};
    if(!p.program) {
        p.ranges=std::move(mesh.ranges);
        std::sort(p.ranges.begin(),p.ranges.end(),[](const auto& a,const auto& b){return a.slot<b.slot;});
        p.vertices=std::move(mesh.moving);p.deformed=p.vertices;
    }
}
void FoliageRenderer::vertex(const FoliageVertex& v,bool animated) const {
    glTexCoord2f(v.uv.u,v.uv.v);
    glColor3f(v.color.r,v.color.g,v.color.b);glNormal3d(v.normal.x,v.normal.y,v.normal.z);
    if(animated && impl_->program) {
        impl_->coord(0x84C1,v.flex,v.height,0,v.slot);
        // Bake the phase of the shoot into its immutable mesh. Only the
        // shared clock changes per frame (the sine angle addition formula).
        if(v.flutter!=0) impl_->coord(0x84C2,v.flutter*std::cos(v.leafPhase),v.flutter*std::sin(v.leafPhase),0,0);
        else impl_->coord(0x84C2,0,0,0,0);
    }
    glVertex3d(v.position.x,v.position.y,v.position.z);
}
void FoliageRenderer::prepare(const FoliageField& field,const FoliageDraw& view,const ViewFrustum& frustum) {
    auto& p=*impl_;p.view=view;p.animated=0;
    p.fixed.select(frustum,0,0);
    const auto visible=p.moving.select(frustum,view.enabled ? field.maximumBend() : 0,view.enabled ? view.windStrength : 0);
    if(!view.enabled || !visible || field.roots().empty()) return;
    const auto local=[&](Vec3 v){return Vec3{dot(v,view.east),dot(v,view.up),dot(v,view.north)};};
    if(p.program) {
        std::array<unsigned short,maximumFoliagePlants*4> pixels{};pixels.fill(32768);
        for(std::size_t i=0;i<field.roots().size();++i) {
            const double angle=field.roots()[i].phase+view.phase;
            const double gust=.78+.18*std::sin(angle)+.04*std::sin(2*angle);
            const auto v=local(field.bend(i))*gust;const std::array values{v.x,v.y,v.z};
            for(std::size_t j=0;j<3;++j) pixels[i*4+j]=static_cast<unsigned short>(std::lround(clamp(values[j],-1.,1.)*32767+32768));
        }
        glPushAttrib(GL_TEXTURE_BIT);glBindTexture(GL_TEXTURE_2D,p.texture);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,1,GL_RGBA,GL_UNSIGNED_SHORT,pixels.data());glPopAttrib();
        p.animated=visible;
    } else {
        // Complete plants are selected in nearest-first mesh order. Distant
        // geometry stays cached; never perform unbounded CPU vertex animation.
        for(const auto& r:p.ranges) {
            if(r.slot<0 || p.animated+r.count>18000) continue;
            const auto bend=local(field.bend(static_cast<std::size_t>(r.slot)));
            const double angle=field.roots()[static_cast<std::size_t>(r.slot)].phase+view.phase;
            const double gust=.78+.18*std::sin(angle)+.04*std::sin(2*angle);
            for(std::size_t i=r.first;i<r.first+r.count;++i) {
                const auto& v=p.vertices[i];auto& out=p.deformed[i];out=v;
                if(view.enabled) {
                    out.position=bendFoliagePosition(v,bend,view.phase,view.windStrength);
                    out.normal.y-=dot(v.normal,bend)*gust*2*v.flex;out.normal=normalized(out.normal);
                }
            }
            p.animated+=r.count;
        }
        if(!view.enabled) p.animated=0;
    }
}
void FoliageRenderer::draw(int wire) {
    auto& p=*impl_;
    glPushAttrib(GL_ENABLE_BIT|GL_LIGHTING_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,p.atlas);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,static_cast<float>(leafCutoff)/255.f);
    // Nearby plants write depth first so their leaves occlude the distant forest.
    if(p.animated==0) {p.moving.draw();p.fixed.draw();glPopAttrib();return;}
    if(p.program) {
        GLint previous=0;glGetIntegerv(0x8B8D,&previous);p.use(p.program);p.activate(0x84C1);glBindTexture(GL_TEXTURE_2D,p.texture);p.activate(0x84C0);
        p.real(p.uniforms[0],static_cast<float>(p.view.windStrength*std::sin(3*p.view.phase)));
        p.real(p.uniforms[1],static_cast<float>(p.view.windStrength*std::cos(3*p.view.phase)));
        p.real(p.uniforms[2],glIsEnabled(GL_LIGHT0) ? 1.f : 0.f);p.real(p.uniforms[3],glIsEnabled(GL_LIGHT1) ? 1.f : 0.f);p.real(p.uniforms[4],static_cast<float>(wire));
        GLint fog=0;glGetIntegerv(GL_FOG_MODE,&fog);p.real(p.uniforms[5],!glIsEnabled(GL_FOG) ? 0.f : fog==GL_EXP ? 1.f : 2.f);
        p.real(p.uniforms[6],p.view.enabled ? 1.f : 0.f);
        p.moving.draw();p.use(static_cast<GLuint>(previous));
    } else if(!p.deformed.empty()) {
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_NORMAL_ARRAY);
        if(wire==0) glEnableClientState(GL_COLOR_ARRAY);else glDisableClientState(GL_COLOR_ARRAY);
        const auto* bytes=reinterpret_cast<const unsigned char*>(p.deformed.data());
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2,GL_FLOAT,sizeof(FoliageVertex),bytes+offsetof(FoliageVertex,uv));
        glVertexPointer(3,GL_DOUBLE,sizeof(FoliageVertex),bytes+offsetof(FoliageVertex,position));
        glNormalPointer(GL_DOUBLE,sizeof(FoliageVertex),bytes+offsetof(FoliageVertex,normal));
        glColorPointer(3,GL_FLOAT,sizeof(FoliageVertex),bytes+offsetof(FoliageVertex,color));
        for(const auto id:p.moving.visible) {
            const auto i=id-p.moving.first;
            glDrawArrays(GL_TRIANGLES,static_cast<GLint>(i*foliageBatchVertices),static_cast<GLsizei>(p.moving.batches[i].count));
        }
        glPopClientAttrib();
    }
    p.fixed.draw();
    glPopAttrib();
}
} // namespace planet

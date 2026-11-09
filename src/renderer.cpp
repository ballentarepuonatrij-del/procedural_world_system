#include "planet/renderer.hpp"
#include <chrono>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cstdlib>

namespace planet {
namespace {
struct DrawTimer {
    double& milliseconds;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    ~DrawTimer() { milliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); }
};
}
namespace {
void perspective(double fov,double aspect,double nearPlane,double farPlane) {
    const double top = nearPlane*std::tan(fov/2);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-top*aspect,top*aspect,-top,top,nearPlane,farPlane);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}
void camera(Vec3 eye,Vec3 forward,Vec3 up) {
    const Vec3 f=normalized(forward), side=normalized(cross(f,up)), u=cross(side,f);
    const GLdouble matrix[]{side.x,u.x,-f.x,0,side.y,u.y,-f.y,0,side.z,u.z,-f.z,0,0,0,0,1};
    glMultMatrixd(matrix); glTranslated(-eye.x,-eye.y,-eye.z);
}
void rect(float x,float y,float w,float h,Color c,float alpha=1) {
    glColor4f(c.r,c.g,c.b,alpha);
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y); glVertex2f(x+w,y+h); glVertex2f(x,y+h);
    glEnd();
}
std::string fixed(double value,int precision=1) {
    std::ostringstream out; out << std::fixed << std::setprecision(precision) << value; return out.str();
}
GLuint makeFont(HDC dc,int size,int weight) {
    const HFONT font = CreateFontW(size,0,0,0,weight,FALSE,FALSE,FALSE,ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FF_DONTCARE,L"Consolas");
    if (!font) throw std::runtime_error("Cannot create HUD font");
    const auto previous = SelectObject(dc,font);
    const GLuint base=glGenLists(96);
    const bool ok=base!=0 && wglUseFontBitmapsW(dc,32,96,base);
    SelectObject(dc,previous); DeleteObject(font);
    if (!ok) { if (base) glDeleteLists(base,96); throw std::runtime_error("Cannot create OpenGL font"); }
    return base;
}
// Small meshes in metres, generated into one cached display list. The same
// triangles are reused for the solid and wireframe passes.

}
Renderer::Renderer(HDC dc) {
    font_=makeFont(dc,15,FW_NORMAL); headingFont_=makeFont(dc,28,FW_BOLD);
    terrain_=glGenLists(1); terrainGrid_=glGenLists(1); globe_=glGenLists(1); objects_=glGenLists(1); caves_=glGenLists(1);weatherMap_=glGenLists(1);shadows_=glGenLists(1);
    waterMap_=glGenLists(1);
    if (!terrain_ || !terrainGrid_ || !globe_ || !objects_ || !caves_ || !weatherMap_ || !shadows_ || !waterMap_) throw std::runtime_error("Cannot allocate terrain display lists");
    glGetIntegerv(GL_STENCIL_BITS,&stencilBits_);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glGenTextures(1,&riverTexture_);glBindTexture(GL_TEXTURE_2D,riverTexture_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    std::vector<unsigned char> ripples(128*128);
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) ripples[static_cast<std::size_t>(y*128+x)]=static_cast<unsigned char>(215+24*std::cos(tau*x/32+1.5*std::sin(tau*y/64))+12*randomUnit(static_cast<std::uint64_t>(y*128+x)));
    for(int size=128,level=0;;size/=2,++level) {
        glTexImage2D(GL_TEXTURE_2D,level,GL_LUMINANCE,size,size,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,ripples.data());
        if(size==1) break;
        std::vector<unsigned char> next(static_cast<std::size_t>(size*size/4));
        for(int y=0;y<size/2;++y) for(int x=0;x<size/2;++x) next[static_cast<std::size_t>(y*(size/2)+x)]=static_cast<unsigned char>((ripples[static_cast<std::size_t>(2*y*size+2*x)]+ripples[static_cast<std::size_t>(2*y*size+2*x+1)]+ripples[static_cast<std::size_t>((2*y+1)*size+2*x)]+ripples[static_cast<std::size_t>((2*y+1)*size+2*x+1)])/4);
        ripples=std::move(next);
    }
    const auto* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const auto* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    stencilWrap_=(version && std::atof(version)>=1.4) || (extensions && std::strstr(extensions,"GL_EXT_stencil_wrap"));
    glEnable(GL_DEPTH_TEST); glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_LIGHT0);
    const GLfloat ambient[]{0.3f,0.3f,0.3f,1}; glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
    // A tiny generated material gives a visual metre scale without external assets.
    glGenTextures(1,&detailTexture_); glBindTexture(GL_TEXTURE_2D,detailTexture_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    std::vector<unsigned char> pixels(128*128);
    for(std::size_t i=0;i<pixels.size();++i)
        pixels[i]=static_cast<unsigned char>(183+72*randomUnit(i+2718));
    int size=128,level=0;
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    while(true) {
        glTexImage2D(GL_TEXTURE_2D,level,GL_LUMINANCE,size,size,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,pixels.data());
        if(size==1) break;
        std::vector<unsigned char> next(static_cast<std::size_t>(size*size/4));
        for(int y=0;y<size/2;++y) for(int x=0;x<size/2;++x) {
            const auto index=static_cast<std::size_t>(2*y*size+2*x);
            next[static_cast<std::size_t>(y*(size/2)+x)]=static_cast<unsigned char>((pixels[index]+pixels[index+1]+pixels[index+size]+pixels[index+size+1])/4);
        }
        pixels=std::move(next); size/=2; ++level;
    }
    // A generated, soft density footprint; cloud water and motion come from Weather.
    glGenTextures(1,&cloudTexture_); glBindTexture(GL_TEXTURE_2D,cloudTexture_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    std::vector<unsigned char> cloudPixels(64*64*4);
    const Noise cloudNoise(7117);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const double px=(x+0.5)/32-1,py=(y+0.5)/32-1;
        const double radius=std::hypot(px,py)+0.10*cloudNoise.fbm({px*4,py*4,0.5},3);
        const double density=(1-smoothstep(0.40,0.98,radius))*std::exp(-0.5*radius*radius);
        const auto i=static_cast<std::size_t>((y*64+x)*4);
        const auto shade=static_cast<unsigned char>(205+50*(y/63.0));
        cloudPixels[i]=shade; cloudPixels[i+1]=shade; cloudPixels[i+2]=shade;
        cloudPixels[i+3]=static_cast<unsigned char>(255*clamp(density,0.0,1.0));
    }
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,cloudPixels.data());
}
Renderer::~Renderer() {
    discardUpload();if(terrainChunks_) glDeleteLists(terrainChunks_,terrainChunksCount_);
    glDeleteLists(terrain_,1); glDeleteLists(globe_,1);
    glDeleteLists(terrainGrid_,1);
    glDeleteLists(objects_,1); glDeleteLists(caves_,1);
    glDeleteLists(weatherMap_,1);
    glDeleteLists(shadows_,1);
    glDeleteLists(waterMap_,1);
    glDeleteTextures(1,&riverTexture_);
    glDeleteLists(font_,96); glDeleteLists(headingFont_,96);
    glDeleteTextures(1,&detailTexture_);
    glDeleteTextures(1,&cloudTexture_);
}
std::string Renderer::device() const {
    const auto* vendor=glGetString(GL_RENDERER);
    return vendor ? reinterpret_cast<const char*>(vendor) : "unknown";
}
void Renderer::buildTerrain(const World& world,const Explorer& explorer,const Hydrology& water,double seconds) {
    DrawTimer timer{timings_.terrain};
    uploadTerrain(makeTerrainMesh(world,water,explorer.up(),explorer.altitude(),seconds));
}
void Renderer::uploadTerrain(TerrainMesh mesh) {
    discardUpload();
    const auto compile=[](GLuint list,const std::vector<TerrainVertex>& vertices) {
        glNewList(list,GL_COMPILE);glBegin(GL_TRIANGLES);
        for(const auto& v:vertices) {
            glNormal3d(v.normal.x,v.normal.y,v.normal.z);glColor3f(v.color.r,v.color.g,v.color.b);
            glTexCoord2d(v.u,v.v);glVertex3d(v.position.x,v.position.y,v.position.z);
        }
        glEnd();glEndList();
    };
    compile(terrain_,mesh.terrain);compile(caves_,mesh.caves);
    glNewList(terrainGrid_,GL_COMPILE);glBegin(GL_LINES);
    for(const auto& v:mesh.grid) {glColor4f(.20f,.82f,1.f,v.alpha);glVertex3d(v.position.x,v.position.y,v.position.z);}
    glEnd();glEndList();glNewList(shadows_,GL_COMPILE);glEndList();
    if(terrainChunks_) glDeleteLists(terrainChunks_,terrainChunksCount_);
    terrainChunks_=0;terrainChunksCount_=0;preparedFlora_.reset();preparedShadow_=false;
    adoptTerrain(std::move(mesh));
}
void Renderer::adoptTerrain(TerrainMesh mesh) {
    anchor_=mesh.anchor;east_=mesh.east;north_=mesh.north;terrainStep_=mesh.spacing;terrainEyeHeight_=mesh.eyeHeight;
    waterRevision_=mesh.waterRevision;waterSteps_=mesh.waterSteps;lastTerrainTime_=mesh.seconds;
    terrainCasters_=std::move(mesh.casters);waterVertices_=std::move(mesh.waterVertices);waterTriangles_=std::move(mesh.waterTriangles);
    objectSites_=std::move(mesh.plants);riverSegments_=std::move(mesh.rivers);caveCount_=mesh.caves.size()/3;
    lastFloraDay_=-1e30;shadowDirty_=true;dirty_=false;
}
void Renderer::discardUpload() {
    if(!meshUpload_) return;
    std::size_t count=0;for(auto n:meshUpload_->chunks) count+=n;
    if(meshUpload_->lists) glDeleteLists(meshUpload_->lists,static_cast<GLsizei>(count));
    meshUpload_.reset();
}
void Renderer::beginUpload(SceneMesh scene) {
    discardUpload();MeshUpload upload;upload.scene=std::move(scene);upload.epoch=terrainEpoch_;
    constexpr std::size_t chunk=3072;
    const auto& mesh=upload.scene.terrain;
    const std::array sizes{mesh.terrain.size(),mesh.caves.size(),mesh.grid.size(),upload.scene.shadows.size()*3};
    std::size_t count=0;
    for(std::size_t i=0;i<sizes.size();++i) {upload.chunks[i]=(sizes[i]+chunk-1)/chunk;count+=upload.chunks[i];}
    upload.lists=glGenLists(static_cast<GLsizei>(count));
    if(!upload.lists) throw std::runtime_error("Cannot allocate streamed terrain display lists");
    meshUpload_=std::move(upload);
}
void Renderer::advanceUpload(const Flora& flora,Vec3 sunlight) {
    if(!meshUpload_) return;
    if(meshUpload_->epoch!=terrainEpoch_) {discardUpload();++terrainDiscarded_;return;}
    DrawTimer timer{timings_.terrain};const auto start=std::chrono::steady_clock::now();
    auto& upload=*meshUpload_;auto& scene=upload.scene;auto& mesh=scene.terrain;
    std::size_t count=0;for(auto n:upload.chunks) count+=n;
    constexpr std::size_t chunk=3072;
    // Each list is closed before drawing a frame. Never expose a partially
    // uploaded mesh, and budget driver work independently of CPU generation.
    do {
        std::size_t section=0,part=upload.next;
        while(section<3 && part>=upload.chunks[section]) part-=upload.chunks[section++];
        const auto first=part*chunk;
        glNewList(upload.lists+static_cast<GLuint>(upload.next),GL_COMPILE);glBegin(section==2 ? GL_LINES : GL_TRIANGLES);
        if(section<2) {
            const auto& vertices=section==0 ? mesh.terrain : mesh.caves;
            for(std::size_t i=first;i<std::min(first+chunk,vertices.size());++i) {const auto& v=vertices[i];
                glNormal3d(v.normal.x,v.normal.y,v.normal.z);glColor3f(v.color.r,v.color.g,v.color.b);
                glTexCoord2d(v.u,v.v);glVertex3d(v.position.x,v.position.y,v.position.z);
            }
        } else if(section==2) {
            for(std::size_t i=first;i<std::min(first+chunk,mesh.grid.size());++i) {const auto& v=mesh.grid[i];
                glColor4f(.20f,.82f,1.f,v.alpha);glVertex3d(v.position.x,v.position.y,v.position.z);
            }
        } else for(std::size_t i=first;i<std::min(first+chunk,scene.shadows.size()*3);++i) {
            const auto& t=scene.shadows[i/3];const auto p=i%3==0 ? t.a : i%3==1 ? t.b : t.c;glVertex3d(p.x,p.y,p.z);
        }
        glEnd();glEndList();++upload.next;
    } while(upload.next<count && std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<3);
    if(upload.next<count) return;
    const std::array<GLuint,4> parents{terrain_,caves_,terrainGrid_,shadows_};std::size_t offset=0;
    for(std::size_t section=0;section<parents.size();++section) {
        glNewList(parents[section],GL_COMPILE);
        for(std::size_t i=0;i<upload.chunks[section];++i) glCallList(upload.lists+static_cast<GLuint>(offset+i));
        glEndList();offset+=upload.chunks[section];
    }
    if(terrainChunks_) glDeleteLists(terrainChunks_,terrainChunksCount_);
    terrainChunks_=upload.lists;terrainChunksCount_=static_cast<GLsizei>(count);upload.lists=0;
    const Vec3 localSun{dot(sunlight,mesh.east),dot(sunlight,mesh.anchor),dot(sunlight,mesh.north)};
    const bool sameFlora=std::abs(flora.days()-scene.flora.days())<.25 &&
        std::abs(flora.calendarDay()-scene.flora.calendarDay())<.25 && flora.young()==scene.flora.young();
    preparedFlora_=sameFlora ? std::optional<Flora>{scene.flora} : std::nullopt;
    preparedShadow_=sameFlora && scene.hasShadows && length(localSun-scene.sun)<=radians(.02);
    shadowSun_=scene.sun;shadowCount_=scene.shadows.size();
    adoptTerrain(std::move(mesh));meshUpload_.reset();++terrainUploads_;
}
void Renderer::buildObjects(const World& world,const Flora& flora) {
    DrawTimer timer{timings_.plants};
    objectCasters_.clear();shadowDirty_=true;
    glNewList(objects_,GL_COMPILE);glBegin(GL_TRIANGLES);
    detailCount_=0;plantCount_=0;marineCount_=0;freshwaterCount_=0;plantHeightSum_=0;focusPlant_.reset();
    floweringCount_=buddingCount_=seedHeadCount_=0;
    std::array<bool,speciesCount> species{};double nearest=1e30;
    const auto local=[&](Vec3 p) { return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)}; };
    for(const auto& site:objectSites_) {
        const auto object=grownObject(site.object,&flora);
        if(object.height<.015) continue;
        const double distance=length(object.up-anchor_)*world.config().radius;
        const auto mesh=detailMesh(object,site.base,local(object.up),local(east(object.up)),distance>(object.kind==DetailKind::Grass || object.kind==DetailKind::Shrub ? 24 : 100));
        for(const auto& face:mesh) {
            const auto& t=face.triangle;const auto n=normalized(cross(t.b-t.a,t.c-t.a));
            glNormal3d(n.x,n.y,n.z);glColor3f(face.color.r,face.color.g,face.color.b);
            for(const auto v:{t.a,t.b,t.c}) glVertex3d(v.x,v.y,v.z);
            if(distance<=240 && object.castsSurfaceShadow()) objectCasters_.push_back(t);
        }
        ++detailCount_;
        if(object.species!=Species::None) {
            ++plantCount_;species[static_cast<unsigned>(object.species)]=true;plantHeightSum_+=object.height;
            if(plantTraits(object.species).aquatic) {if(freshwaterPlant(object.species)) ++freshwaterCount_;else ++marineCount_;}
            if(object.flowers.bloom>.02) ++floweringCount_;
            if(object.flowers.buds>.02) ++buddingCount_;
            if(object.flowers.seeds>.02) ++seedHeadCount_;
            if(distance<nearest) { nearest=distance;focusPlant_=object; }
        }
    }
    glEnd();glEndList();
    plantSpeciesCount_=static_cast<std::size_t>(std::count(species.begin(),species.end(),true));
    lastFloraDay_=flora.days();lastFloraCalendar_=flora.calendarDay();lastYoung_=flora.young();
}
void Renderer::buildGlobe(const World& world,double seconds) {
    constexpr int latitudes=100,longitudes=200;
    glNewList(globe_,GL_COMPILE);
    const auto emit=[&](int x,int y) {
        const Vec3 p=direction(-pi/2+pi*y/latitudes,tau*x/longitudes);
        const auto sample=world.sample(p,seconds);
        const auto color=world.color(sample);
        // True radius, without vertical exaggeration.
        const Vec3 v=p*(1+(sample.ocean || sample.freshwater ? sample.waterLevel : sample.elevation)/world.config().radius);
        glColor3f(color.r,color.g,color.b); glNormal3d(p.x,p.y,p.z); glVertex3d(v.x,v.y,v.z);
    };
    for(int y=0;y<latitudes;++y) {
        glBegin(GL_TRIANGLE_STRIP);
        for(int x=0;x<=longitudes;++x) { emit(x,y+1); emit(x,y); }
        glEnd();
    }
    glEndList(); lastGlobeTime_=seconds; globeDirty_=false;
}
void Renderer::drawSky(const World& world,const Explorer& explorer,const ViewState& view) {
    DrawTimer timer{timings_.sky};
    starCount_=0;
    if(!view.skyEnabled || (!view.globe && explorer.underwater())) return;
    if(stars_.empty() || starSeed_!=world.config().seed) {starSeed_=world.config().seed;stars_=starCatalog(starSeed_);}
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glMatrixMode(GL_MODELVIEW);glPushMatrix();
    const double fov=view.globe ? GlobeCamera::fov : radians(72),aspect=static_cast<double>(view.width)/view.height;
    perspective(fov,aspect,.1,2000);
    const auto forward=view.globe ? -view.globeCamera.outward : explorer.forward();
    const auto up=view.globe ? view.globeCamera.up : explorer.up();
    const auto right=normalized(cross(forward,up)),vertical=normalized(cross(right,forward));
    camera({},forward,up);
    if(!view.globe) {
        glBegin(GL_QUADS);
        for(int row=0;row<24;++row) for(int col=0;col<64;++col) {
            for(const auto xy:std::array<std::array<int,2>,4>{{{col,row},{col+1,row},{col+1,row+1},{col,row+1}}}) {
                const auto p=direction(-pi/2+pi*xy[1]/24,tau*xy[0]/64);const auto color=skyColor(lighting_,p);
                glColor3f(color.r,color.g,color.b);glVertex3d(p.x*1000,p.y*1000,p.z*1000);
            }
        }
        glEnd();
    }
    const double visibility=view.globe ? 1 : lighting_.starVisibility;
    if(visibility>.001) {
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);glEnable(GL_POINT_SMOOTH);
        for(const auto& star:stars_) {
            const auto p=inertialToPlanet(star.inertialDirection,world.config(),view.seconds);
            const double height=std::asin(clamp(dot(p,explorer.up()),-1.0,1.0)),depth=dot(p,forward);
            if(!view.globe && height<lighting_.horizon) continue;
            if(depth<=0 || std::abs(dot(p,right))>depth*std::tan(fov/2)*aspect || std::abs(dot(p,vertical))>depth*std::tan(fov/2)) continue;
            const double extinction=view.globe ? 1 : std::exp(-.06*relativeAirMass(height)*lighting_.airDensity);
            const double strength=visibility*extinction*clamp(std::pow(10.,-.15*(star.magnitude-1)),.08,1.0);
            glPointSize(static_cast<float>(star.magnitude<1.5 ? 2.7 : (star.magnitude<3 ? 1.8 : 1.0)));
            glColor4f(star.color.r,star.color.g,star.color.b,static_cast<float>(strength));
            glBegin(GL_POINTS);glVertex3d(p.x*999,p.y*999,p.z*999);glEnd();++starCount_;
        }
        glDisable(GL_POINT_SMOOTH);glDisable(GL_BLEND);
    }
    if(view.globe || lighting_.direct>.0001) {
        const auto sun=lighting_.sun,solarRight=east(sun),solarUp=normalized(cross(solarRight,sun));
        const double radius=std::tan(lighting_.angularRadius)*998;
        const double peak=view.globe ? 1 : std::max({lighting_.sunlight.r,lighting_.sunlight.g,lighting_.sunlight.b,.001f});
        const double intensity=view.globe ? 1 : clamp(lighting_.direct*4,0.0,1.0);
        const Color color=view.globe ? Color{1,.97f,.90f} : Color{static_cast<float>(lighting_.sunlight.r/peak),static_cast<float>(lighting_.sunlight.g/peak),static_cast<float>(lighting_.sunlight.b/peak)};
        glBegin(GL_TRIANGLE_FAN);
        glColor3f(static_cast<float>(color.r*intensity),static_cast<float>(color.g*intensity),static_cast<float>(color.b*intensity));
        glVertex3d(sun.x*998,sun.y*998,sun.z*998);
        for(int i=0;i<=40;++i) {
            const auto p=sun*998+(solarRight*std::cos(tau*i/40)+solarUp*std::sin(tau*i/40))*radius;
            glColor3f(static_cast<float>(color.r*intensity*.65),static_cast<float>(color.g*intensity*.65),static_cast<float>(color.b*intensity*.65));
            glVertex3d(p.x,p.y,p.z);
        }
        glEnd();
    }
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopAttrib();
}
void Renderer::prepareFauna(const World& world,const Explorer& explorer,const Fauna& fauna,bool shadows,Vec3 sun) {
    DrawTimer timer{timings_.fauna};deerFaces_.clear();deerShadows_.clear();deerCount_=0;
    if(explorer.underground() || !world.modules().enabled(Module::Fauna)) return;
    const auto local=[&](Vec3 p){return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)};};
    std::vector<Triangle> casters;
    for(const auto& deer:fauna.visible(explorer.up())) {
        const double range=length(deer.up-explorer.up())*world.config().radius;
        const auto delta=deer.up*(world.config().radius+deer.elevation+1)-explorer.up()*(world.config().radius+explorer.altitude());
        if(dot(delta,explorer.forward())<-3) continue;
        const auto right=normalized(cross(deer.up,deer.heading));
        const auto base=deer.up*(world.config().radius+deer.elevation)-anchor_*world.config().radius;
        const auto transform=[&](Vec3 v){return local(base+right*v.x+deer.up*v.y+deer.heading*v.z);};
        for(const auto& face:deerMesh(deer,range>60)) {
            const Triangle t{transform(face.triangle.a),transform(face.triangle.b),transform(face.triangle.c)};
            deerFaces_.push_back({t,face.color});if(shadows && range<160) casters.push_back(t);
        }
        ++deerCount_;
    }
    if(!casters.empty()) deerShadows_=shadowVolume(casters,local(sun),150,.015);
}
void Renderer::emitFauna() {
    glBegin(GL_TRIANGLES);
    for(const auto& f:deerFaces_) {
        const auto& t=f.triangle;const auto n=normalized(cross(t.b-t.a,t.c-t.a));glNormal3d(n.x,n.y,n.z);glColor3f(f.color.r,f.color.g,f.color.b);
        for(const auto p:{t.a,t.b,t.c}) glVertex3d(p.x,p.y,p.z);
    }
    glEnd();
}
void Renderer::drawShadows(Vec3 sun,bool textured) {
    DrawTimer timer{timings_.shadows};
    if(shadowDirty_ || length(sun-shadowSun_)>radians(.02)) {
        DrawTimer buildTimer{timings_.shadowBuild};
        std::vector<Triangle> casters=terrainCasters_;casters.insert(casters.end(),objectCasters_.begin(),objectCasters_.end());
        const auto volume=shadowVolume(casters,sun);shadowCount_=volume.size();
        glNewList(shadows_,GL_COMPILE);glBegin(GL_TRIANGLES);
        for(const auto& t:volume) for(const auto p:{t.a,t.b,t.c}) glVertex3d(p.x,p.y,p.z);
        glEnd();glEndList();shadowSun_=sun;shadowDirty_=false;
    }
    if(!shadowCount_ && deerShadows_.empty()) return;
    shadowsUsed_=true;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
    // Far caps can round to the receiver's depth in a 24-bit buffer. Equal depth
    // must fail the volume test, and a slope-scaled offset keeps lit caps behind
    // their receiver even at grazing angles; a fixed centimetre bias cannot do this.
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1,2);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDepthMask(GL_FALSE);glDepthFunc(GL_LESS);
    glEnable(GL_STENCIL_TEST);glStencilMask(255);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);glStencilFunc(GL_ALWAYS,0,255);
    glEnable(GL_CULL_FACE);glFrontFace(GL_CCW);
    // Closed-volume depth-fail counting also works with the eye inside a shadow.
    const auto dynamicVolumes=[&] {glBegin(GL_TRIANGLES);for(const auto& t:deerShadows_) for(const auto p:{t.a,t.b,t.c}) glVertex3d(p.x,p.y,p.z);glEnd();};
    glCullFace(GL_FRONT);glStencilOp(GL_KEEP,stencilWrap_ ? 0x8507 : GL_INCR,GL_KEEP);glCallList(shadows_);dynamicVolumes();
    glCullFace(GL_BACK);glStencilOp(GL_KEEP,stencilWrap_ ? 0x8508 : GL_DECR,GL_KEEP);glCallList(shadows_);dynamicVolumes();
    glPopAttrib();
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glEnable(GL_STENCIL_TEST);glStencilMask(0);glStencilFunc(GL_NOTEQUAL,0,255);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    glDepthMask(GL_FALSE);glDepthFunc(GL_LEQUAL);glDisable(GL_BLEND);glEnable(GL_LIGHTING);glDisable(GL_LIGHT0);
    if(textured) {glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,detailTexture_);}
    glCallList(terrain_);glDisable(GL_TEXTURE_2D);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE);glCallList(objects_);emitFauna();
    glPopAttrib();
}
void Renderer::draw(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view) {
    timings_={};deerCount_=0;deerFaces_.clear();deerShadows_.clear();
    const double travelSpeed=dirty_ || length(previousObserver_)<.5 || view.frameSeconds<=0 ? 0 :
        length(explorer.up()-previousObserver_)*world.config().radius/view.frameSeconds;
    previousObserver_=explorer.up();
    const double detailStep=explorer.flying ? std::ldexp(2.,clamp(static_cast<int>(std::floor(std::log2(std::max(1.,travelSpeed/80)))),0,10)) : 2;
    if(detailStep>flightDetailStep_ || travelSpeed<flightDetailStep_*25 || !explorer.flying) flightDetailStep_=detailStep;
    const auto& water=simulation.water;
    cloudCount_=0;waterCount_=0;rainCount_=0;lightningCount_=0;stormMarkerCount_=0;riverTriangles_=0;
    glViewport(0,0,view.width,view.height);
    const auto sun=orbit(world.config(),view.seconds);
    const double rain=simulation.weather.sample(explorer.up()).rainRate*3600+simulation.rainExperiment();
    const bool atmosphere=world.modules().enabled(Module::Atmosphere);
    SkyConditions conditions;conditions.atmosphere=atmosphere;conditions.altitude=explorer.altitude();conditions.rainMmH=rain;
    if(simulation.weather.cloudsEnabled() && explorer.altitude()<simulation.weather.layerAltitude()) {
        conditions.cloudWater=simulation.weather.sample(explorer.up()).cloudWater;
        const auto eye=explorer.up()*(world.config().radius+explorer.altitude());
        const double shell=world.config().radius+simulation.weather.layerAltitude(),along=dot(eye,sun.sunDirection);
        const double distance=-along+std::sqrt(std::max(0.0,along*along+shell*shell-dot(eye,eye)));
        const auto cloudPoint=normalized(eye+sun.sunDirection*distance);const auto air=simulation.weather.sample(cloudPoint);
        conditions.sunCloudWater=air.available ? air.cloudWater : simulation.global.sample(cloudPoint).cloud;
    }
    lighting_=lighting(world.config(),explorer.up(),view.seconds,conditions);shadowsUsed_=false;starCount_=0;
    const double daylight=lighting_.daylight*(1-.55*lighting_.cloudCover);
    const bool wireframe=view.surfaceStyle==SurfaceStyle::Wireframe && !view.globe;
    const auto liquid=liquidAt(world,&water,&simulation.motion,explorer.up());
    const bool submerged=explorer.underwater() && !view.globe;
    const double diveDepth=submerged ? std::max(0.0,liquid.level-explorer.altitude()) : 0;
    const double waterLight=std::exp(-0.045*diveDepth)*(0.015+0.985*daylight);
    const GLfloat waterFog[]{0.015f*static_cast<float>(waterLight),0.14f*static_cast<float>(waterLight),0.19f*static_cast<float>(waterLight),1};
    if(submerged) glClearColor(waterFog[0],waterFog[1],waterFog[2],1);
    else if(wireframe) glClearColor(0.008f,0.014f,0.035f,1);
    else glClearColor(0,0,0,1);
    glStencilMask(255);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    drawSky(world,explorer,view);
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING); glDisable(GL_BLEND); glDisable(GL_LIGHT1);
    const GLfloat sunColor[]{view.globe ? 1.f : lighting_.sunlight.r*static_cast<float>(submerged ? .35*waterLight : 1),
        view.globe ? .98f : lighting_.sunlight.g*static_cast<float>(submerged ? .75*waterLight : 1),
        view.globe ? .95f : lighting_.sunlight.b*static_cast<float>(submerged ? waterLight : 1),1};
    glLightfv(GL_LIGHT0,GL_DIFFUSE,sunColor);
    if(view.globe) {
        if(globeDirty_ || std::abs(view.seconds-lastGlobeTime_)>3600) buildGlobe(world,view.seconds);
        perspective(GlobeCamera::fov,static_cast<double>(view.width)/view.height,0.1,20);
        camera(view.globeCamera.outward*GlobeCamera::distance,-view.globeCamera.outward,view.globeCamera.up);
        const GLfloat light[]{static_cast<float>(sun.sunDirection.x),static_cast<float>(sun.sunDirection.y),static_cast<float>(sun.sunDirection.z),0};
        glLightfv(GL_LIGHT0,GL_POSITION,light);
        const GLfloat ambient[]{0.006f,0.010f,0.018f,1};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
        glDisable(GL_FOG); glCallList(globe_); glDisable(GL_LIGHTING);
        drawGlobalWeather(simulation.global,view);
        drawGlobalWater(world,view);
        const Vec3 marker=explorer.up()*1.004;
        glPointSize(7); glColor3f(0.6f,1.0f,0.7f); glBegin(GL_POINTS); glVertex3d(marker.x,marker.y,marker.z); glEnd();
        // Sparse meridians: geographic orientation, drawn above the planet surface.
        glColor3f(0.20f,0.37f,0.42f);
        for(int lat=-60;lat<=60;lat+=30) {
            glBegin(GL_LINE_LOOP);
            for(int lon=0;lon<360;lon+=3) { const Vec3 p=direction(radians(lat),radians(lon))*1.002; glVertex3d(p.x,p.y,p.z); }
            glEnd();
        }
    } else {
        if(terrainJob_.valid() && terrainJob_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto mesh=terrainJob_.get();
            if(terrainJobEpoch_==terrainEpoch_) beginUpload(std::move(mesh));
            else ++terrainDiscarded_;
        }
        advanceUpload(simulation.flora,sun.sunDirection);
        if(dirty_) buildTerrain(world,explorer,water,view.seconds);
        else if(!terrainJob_.valid() && !meshUpload_ && (waterRevision_!=water.revision() || (water.enabled() && water.steps()-waterSteps_>=150) ||
           length(explorer.up()-anchor_)*world.config().radius>std::max(12.0,terrainStep_*4) ||
           std::abs(explorer.altitude()-terrainEyeHeight_)>std::max(32.0,terrainStep_*4) ||
           terrainSpacing(world,explorer.up(),explorer.altitude(),flightDetailStep_)!=terrainStep_ || std::abs(view.seconds-lastTerrainTime_)>900)) {
            DrawTimer dispatch{timings_.terrain};
            // One job at a time, with no queue of stale positions. The complete
            // previous mesh stays visible while the next snapshot is prepared.
            if(world.modules().enabled(Module::Rivers)) (void)world.waterNetwork();
            terrainJobEpoch_=terrainEpoch_;++terrainJobs_;
            const bool shadows=view.shadowsEnabled && shadowSupport() && !submerged && lighting_.direct>.002 && dot(sun.sunDirection,explorer.up())>0;
            terrainJob_=std::async(std::launch::async,[worldCopy=world,waterCopy=water,flora=simulation.flora,observer=explorer.up(),altitude=explorer.altitude(),seconds=view.seconds,step=flightDetailStep_,shadows,sunlight=sun.sunDirection] {
                SceneMesh scene;scene.terrain=makeTerrainMesh(worldCopy,waterCopy,observer,altitude,seconds,step);scene.flora=flora;
                const auto& mesh=scene.terrain;const auto local=[&](Vec3 p){return Vec3{dot(p,mesh.east),dot(p,mesh.anchor),dot(p,mesh.north)};};
                scene.sun=local(sunlight);scene.hasShadows=shadows;
                if(shadows) {
                    auto casters=mesh.casters;
                    for(const auto& site:mesh.plants) {
                        const auto object=grownObject(site.object,&flora);const double distance=length(object.up-mesh.anchor)*worldCopy.config().radius;
                        if(object.height<.015 || distance>240 || !object.castsSurfaceShadow()) continue;
                        const bool simple=distance>(object.kind==DetailKind::Grass || object.kind==DetailKind::Shrub ? 24 : 100);
                        for(const auto& face:detailMesh(object,site.base,local(object.up),local(east(object.up)),simple)) casters.push_back(face.triangle);
                    }
                    scene.shadows=shadowVolume(casters,scene.sun);
                }
                return scene;
            });
        }
        if(std::abs(simulation.flora.days()-lastFloraDay_)>=.25 || std::abs(simulation.flora.calendarDay()-lastFloraCalendar_)>=.25 || simulation.flora.young()!=lastYoung_) {
            buildObjects(world,preparedFlora_ ? *preparedFlora_ : simulation.flora);
            if(preparedShadow_) shadowDirty_=false;
            preparedFlora_.reset();preparedShadow_=false;
        }
        perspective(radians(72),static_cast<double>(view.width)/view.height,explorer.underground() || submerged ? 0.08 : 0.25,400000);
        const auto local=[&](Vec3 p) { return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)}; };
        const Vec3 eye=local(explorer.up()*(world.config().radius+explorer.altitude())-anchor_*world.config().radius);
        camera(eye,local(explorer.forward()),local(explorer.up()));
        if(submerged && view.headlamp) {
            glEnable(GL_LIGHT1);
            const GLfloat lamp[]{static_cast<float>(eye.x),static_cast<float>(eye.y),static_cast<float>(eye.z),1},color[]{0.95f,0.94f,0.82f,1},black[]{0,0,0,1};
            glLightfv(GL_LIGHT1,GL_POSITION,lamp); glLightfv(GL_LIGHT1,GL_DIFFUSE,color); glLightfv(GL_LIGHT1,GL_AMBIENT,black);
            glLightf(GL_LIGHT1,GL_CONSTANT_ATTENUATION,1); glLightf(GL_LIGHT1,GL_LINEAR_ATTENUATION,0.02f); glLightf(GL_LIGHT1,GL_QUADRATIC_ATTENUATION,0.0015f);
        }
        const Vec3 localSun=local(sun.sunDirection);
        const GLfloat light[]{static_cast<float>(localSun.x),static_cast<float>(localSun.y),static_cast<float>(localSun.z),0};
        glLightfv(GL_LIGHT0,GL_POSITION,light);
        const GLfloat ambient[]{lighting_.ambient.r,lighting_.ambient.g,lighting_.ambient.b,1};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
        if(submerged) {
            const GLfloat dim[]{0.02f,static_cast<float>(0.10*waterLight),static_cast<float>(0.15*waterLight),1}; glLightModelfv(GL_LIGHT_MODEL_AMBIENT,dim);
            glEnable(GL_FOG); glFogfv(GL_FOG_COLOR,waterFog); glFogi(GL_FOG_MODE,GL_EXP);
            glFogf(GL_FOG_DENSITY,static_cast<float>(0.045+std::min(0.3,liquid.sediment*0.01)));
        } else if(atmosphere && !wireframe) {
            const auto color=skyColor(lighting_,normalized(explorer.forward()-explorer.up()*dot(explorer.forward(),explorer.up())));
            const GLfloat fog[]{color.r,color.g,color.b,1};
            glEnable(GL_FOG); glFogfv(GL_FOG_COLOR,fog); glFogi(GL_FOG_MODE,GL_EXP2);
            glFogf(GL_FOG_DENSITY,static_cast<float>((.000025+.000018*std::sqrt(rain))*std::exp(-std::max(explorer.altitude(),0.0)/8500)));
        } else glDisable(GL_FOG);
        const bool showGrid=view.surfaceStyle!=SurfaceStyle::Natural;
        if(showGrid) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.0f,1.0f); }
        if(wireframe) {
            glDisable(GL_COLOR_MATERIAL);
            const GLfloat material[]{0.018f,0.030f,0.065f,1};
            glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE,material);
        } else { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,detailTexture_); }
        glCallList(terrain_);
        glDisable(GL_TEXTURE_2D);
        // Leaves and marine blades are thin sheets, lit from either side.
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE);
        glCallList(objects_);
        prepareFauna(world,explorer,simulation.fauna,view.shadowsEnabled && shadowSupport() && !submerged && lighting_.direct>.002,sun.sunDirection);
        emitFauna();
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_FALSE);
        if(caveCount_>0) {
            glPushAttrib(GL_ENABLE_BIT|GL_LIGHTING_BIT);
            glDisable(GL_LIGHT0); glDisable(GL_FOG);
            const GLfloat dark[]{0.008f,0.010f,0.014f,1}; glLightModelfv(GL_LIGHT_MODEL_AMBIENT,dark);
            if(view.headlamp) {
                glEnable(GL_LIGHT1);
                const GLfloat lamp[]{static_cast<float>(eye.x),static_cast<float>(eye.y),static_cast<float>(eye.z),1};
                const GLfloat color[]{1.0f,0.94f,0.80f,1},black[]{0,0,0,1};
                glLightfv(GL_LIGHT1,GL_POSITION,lamp); glLightfv(GL_LIGHT1,GL_DIFFUSE,color); glLightfv(GL_LIGHT1,GL_AMBIENT,black);
                glLightf(GL_LIGHT1,GL_CONSTANT_ATTENUATION,1); glLightf(GL_LIGHT1,GL_LINEAR_ATTENUATION,0.015f);
                glLightf(GL_LIGHT1,GL_QUADRATIC_ATTENUATION,0.0018f);
            }
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,detailTexture_); glCallList(caves_); glDisable(GL_TEXTURE_2D);
            glPopAttrib(); // Restore the outdoor fog and light before the shadow receiver pass.
        }
        if(view.shadowsEnabled && shadowSupport() && !submerged && lighting_.direct>.002 && dot(sun.sunDirection,explorer.up())>0)
            drawShadows(localSun,!wireframe);
        glDisable(GL_POLYGON_OFFSET_FILL); glEnable(GL_COLOR_MATERIAL);
        glDisable(GL_TEXTURE_2D); if(!submerged) glDisable(GL_FOG); glDisable(GL_LIGHTING);
        if(showGrid) {
            glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_LINE_SMOOTH); glHint(GL_LINE_SMOOTH_HINT,GL_NICEST); glLineWidth(1.0f);
            glCallList(terrainGrid_);
            if(wireframe || caveCount_>0) {
                glDisable(GL_COLOR_MATERIAL); glEnable(GL_LIGHTING);
                const GLfloat black[]{0,0,0,1}, cyan[]{0.12f,0.64f,0.78f,1};
                glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE,black);
                glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,cyan);
                glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
                if(wireframe) glCallList(objects_);
                glCallList(caves_);
                glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
                glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
                glEnable(GL_COLOR_MATERIAL); glDisable(GL_LIGHTING);
            }
            glDisable(GL_LINE_SMOOTH); glDisable(GL_BLEND);
            glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
        }
        drawWater(world,explorer,simulation,view,daylight);
        drawRivers(world,explorer,simulation,view,daylight);
        if(!submerged) drawWeather(world,explorer,simulation,view,daylight);
    }
    drawHud(world,explorer,simulation,view);
}
void Renderer::drawWater(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight) {
    DrawTimer timer{timings_.water};
    const auto& water=simulation.water; const auto& motion=simulation.motion;
    const bool oceanEnabled=world.modules().enabled(Module::Ocean);
    if(!water.enabled() && !oceanEnabled && !world.modules().enabled(Module::InlandWaters)) return;
    const auto local=[&](Vec3 p) { return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)}; };
    const double radius=world.config().radius,sea=world.config().seaLevel;
    const Vec3 origin=anchor_*radius,eye=explorer.up()*(radius+explorer.altitude());
    const Vec3 sun=orbit(world.config(),view.seconds).sunDirection;
    auto& vertices=waterFrame_;auto& triangles=waterDraw_;
    vertices.assign(waterVertices_.size(),{});triangles.clear();
    waterGround_.resize(waterVertices_.size());waterVisible_.assign(waterVertices_.size(),0);
    const bool wire=view.surfaceStyle==SurfaceStyle::Wireframe;
    if(!wire && world.modules().enabled(Module::Atmosphere)) glEnable(GL_FOG);
    for(std::size_t i=0;i<waterVertices_.size();++i) {
        const auto& v=waterVertices_[i]; if(!v.available) continue;
        const auto ground=waterGround_[i]=water.sample(v.up);
        const bool riverWet=v.river;
        const bool ocean=!riverWet && !v.inland && oceanEnabled && (v.altitude<sea || !ground.available || ground.depth<0.0005);
        const double depth=riverWet ? -.01 : v.inland ? v.level-v.altitude : ocean ? sea-v.altitude : (ground.available ? ground.depth*ground.weight : 0);
        vertices[i].depth=depth;
    }
    // Dry triangles cannot contribute pixels. Their shoreline neighbours still
    // need the original positions/colours for exactly the same wet-edge clipping.
    for(const auto& t:waterTriangles_) if(vertices[t[0]].depth>.0005 || vertices[t[1]].depth>.0005 || vertices[t[2]].depth>.0005)
        for(const auto i:t) waterVisible_[i]=1;
    for(std::size_t i=0;i<waterVertices_.size();++i) {
        const auto& v=waterVertices_[i];if(!v.available || !waterVisible_[i]) continue;
        const auto& ground=waterGround_[i];const double depth=vertices[i].depth;
        const bool ocean=!v.river && !v.inland && oceanEnabled && (v.altitude<sea || !ground.available || ground.depth<0.0005);
        double level=ocean ? sea : v.altitude+depth;
        const auto wave=motion.waves(v.up,std::max(0.0,depth),ocean,v.spacing*2);
        level+=wave.height;
        const Vec3 position=v.up*(radius+level);
        const Vec3 normal=normalized(v.up-wave.gradient),towardEye=normalized(eye-position);
        const double incidence=std::abs(dot(normal,towardEye));
        const double fresnel=0.02+0.98*std::pow(1-incidence,5);
        const Vec3 reflected=normal*(2*dot(normal,sun))-sun;
        const double shine=explorer.underwater() ? 0 : std::pow(std::max(0.0,dot(reflected,towardEye)),72);
        const double mud=ground.available ? 1-std::exp(-ground.sedimentKgM3/12) : 0;
        const double light=.16+.84*daylight;
        const Color clear=v.wetland ? Color{.12f,.20f,.10f} : v.inland ? Color{.035f,.24f,.22f} : Color{0.025f,0.29f,0.37f};
        const Color silt{0.39f,0.29f,0.12f};
        const auto sky=skyColor(lighting_,normal*(2*dot(normal,towardEye))-towardEye);
        Color color=wire ? Color{0.035f,0.11f,0.18f} : Color{
            static_cast<float>(mix(mix(clear.r,silt.r,mud)*light,sky.r,fresnel)+shine*lighting_.sunlight.r*.75),
            static_cast<float>(mix(mix(clear.g,silt.g,mud)*light,sky.g,fresnel)+shine*lighting_.sunlight.g*.75),
            static_cast<float>(mix(mix(clear.b,silt.b,mud)*light,sky.b,fresnel)+shine*lighting_.sunlight.b*.75)};
        const double alpha=(0.20+0.75*(1-std::exp(-std::max(0.0,depth)*0.2))+0.45*fresnel)*smoothstep(0,0.035,depth);
        vertices[i]={local(position-origin),color,static_cast<float>(clamp(alpha,0.0,0.98)),depth};
    }
    for(const auto& t:waterTriangles_) {
        if(!waterVisible_[t[0]] && !waterVisible_[t[1]] && !waterVisible_[t[2]]) continue;
        std::array<WaterDrawVertex,4> clipped{}; std::size_t count=0;
        for(int j=0;j<3;++j) {
            const auto& a=vertices[t[static_cast<std::size_t>(j)]]; const auto& b=vertices[t[static_cast<std::size_t>((j+1)%3)]];
            const bool inside=a.depth>0.0005,next=b.depth>0.0005;
            if(inside) clipped[count++]=a;
            if(inside!=next) {
                const double fraction=(a.depth-0.0005)/(a.depth-b.depth);
                clipped[count++]={a.position+(b.position-a.position)*fraction,
                    {static_cast<float>(mix(a.color.r,b.color.r,fraction)),static_cast<float>(mix(a.color.g,b.color.g,fraction)),static_cast<float>(mix(a.color.b,b.color.b,fraction))},0,0};
            }
        }
        for(std::size_t j=1;j+1<count;++j) { triangles.push_back(clipped[0]); triangles.push_back(clipped[j]); triangles.push_back(clipped[j+1]); ++waterCount_; }
    }
    glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1,-1);
    glBegin(GL_TRIANGLES);
    for(const auto& v:triangles) { glColor4f(v.color.r,v.color.g,v.color.b,v.alpha); glVertex3d(v.position.x,v.position.y,v.position.z); }
    glEnd(); glDisable(GL_POLYGON_OFFSET_FILL);
    if(view.surfaceStyle!=SurfaceStyle::Natural) {
        glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); glBegin(GL_TRIANGLES);
        for(const auto& v:triangles) {
            const double distance=length(v.position-local(eye-origin));
            glColor4f(0.15f,0.72f,0.86f,static_cast<float>((wire ? 0.65 : 0.22)*(1-smoothstep(70,220,distance))));
            glVertex3d(v.position.x,v.position.y,v.position.z);
        }
        glEnd(); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    }
    if(view.waterTracers) {
        glLineWidth(1.5f); glBegin(GL_LINES);
        for(const auto& p:water.tracers()) {
            const auto s=water.sampleLocal(p.x,p.z);
            const double speed=std::hypot(s.velocity.east,s.velocity.north);
            if(!s.available || s.depth<0.001 || s.weight<0.99 || speed<0.005) continue;
            const auto up=water.directionAt(p.x,p.z);
            const Vec3 head=local(water.position(p.x,p.z,s.bed+s.depth+motion.waves(up,s.depth,false).height+0.025)-origin);
            const double duration=std::min(4.0,1.8/speed);
            const auto behind=water.sampleLocal(p.x-s.velocity.east*duration,p.z-s.velocity.north*duration);
            if(!behind.available || behind.depth<0.0005) continue;
            const Vec3 tail=local(water.position(p.x-s.velocity.east*duration,p.z-s.velocity.north*duration,behind.bed+behind.depth+0.025)-origin);
            glColor4f(0.55f,0.93f,1,0.70f); glVertex3d(head.x,head.y,head.z);
            glColor4f(0.55f,0.93f,1,0.08f); glVertex3d(tail.x,tail.y,tail.z);
        }
        if(oceanEnabled && motion.enabled()) for(const auto p:motion.probes()) {
            const auto flow=motion.current(p); const double speed=length(flow);if(speed<0.002) continue;
            const double depth=sea-world.elevation(p);if(depth<0.1) continue;
            const auto behind=normalized(p-flow*(std::min(20.0,2.0/speed)/radius));
            const Vec3 head=local(p*(radius+sea+motion.waves(p,depth).height+0.04)-origin);
            const Vec3 tail=local(behind*(radius+sea+motion.waves(behind,depth).height+0.04)-origin);
            glColor4f(0.60f,0.94f,1,0.85f); glVertex3d(head.x,head.y,head.z);
            glColor4f(0.60f,0.94f,1,0.12f); glVertex3d(tail.x,tail.y,tail.z);
        }
        glEnd(); glLineWidth(1);
    }
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
}
void Renderer::drawRivers(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight) {
    DrawTimer timer{timings_.rivers};
    if(!world.modules().enabled(Module::Rivers) || riverSegments_.empty()) return;
    const auto atlas=world.waterNetwork();const double radius=world.config().radius;
    const auto local=[&](Vec3 p) {const auto r=p-anchor_*radius;return Vec3{dot(r,east_),dot(r,anchor_),dot(r,north_)};};
    const Vec3 eye=explorer.up()*(radius+explorer.altitude());
    const double clock=simulation.motion.seconds(),light=.22+.78*daylight;
    const bool moving=world.modules().enabled(Module::WaterMotion);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glDisable(GL_LIGHTING);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,riverTexture_);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-1);
    const auto emit=[&](const RiverPoint& p,double lateral) {
        const auto up=normalized(p.up+p.left*(lateral*p.width/radius));
        const auto pos=up*(radius+p.level+.008),toward=normalized(eye-pos);
        const double fresnel=.02+.98*std::pow(1-std::abs(dot(toward,up)),5);
        const auto sky=skyColor(lighting_,up*(2*dot(up,toward))-toward);
        const double speed=p.discharge/(.8*p.width*p.depth);
        const Color water{static_cast<float>(mix(.055*light,sky.r,fresnel)),static_cast<float>(mix(.28*light,sky.g,fresnel)),static_cast<float>(mix(.30*light,sky.b,fresnel))};
        const double depth=p.depth*(1-smoothstep(.3,.5,std::abs(lateral)));
        glColor4f(water.r,water.g,water.b,static_cast<float>(clamp((.32+.55*(1-std::exp(-depth*.6))+.3*fresnel)*smoothstep(0,.025,depth),0.,.96)));
        glTexCoord2d((p.along-(moving ? clock*speed : 0))/8,lateral*p.width/8);
        const auto v=local(pos);glVertex3d(v.x,v.y,v.z);
    };
    glBegin(GL_TRIANGLES);
    for(const auto& [r,s]:riverSegments_) {
        const auto a=atlas->point(r,s,0),b=atlas->point(r,s,1);const auto delta=b.up-a.up;
        const double span=length(delta)*radius,t=clamp(dot(explorer.up()-a.up,delta)/dot(delta,delta),0.,1.);
        if(length(atlas->point(r,s,t).up-explorer.up())*radius>2200) continue;
        const double start=std::max(0.,t-2200/std::max(1.,span)),end=std::min(1.,t+2200/std::max(1.,span));
        const double spacing=clamp(std::min(a.width,b.width)*.7,1.,24.);
        const int steps=std::max(1,static_cast<int>(std::ceil((end-start)*span/spacing)));
        auto previous=atlas->point(r,s,start);
        for(int i=1;i<=steps;++i) {
            const auto next=atlas->point(r,s,mix(start,end,static_cast<double>(i)/steps));
            if(next.level<=world.config().seaLevel+.02) {previous=next;continue;}
            for(const auto band:std::array<std::array<double,2>,3>{{{-.5,-.3},{-.3,.3},{.3,.5}}}) {
                emit(previous,band[0]);emit(previous,band[1]);emit(next,band[1]);
                emit(previous,band[0]);emit(next,band[1]);emit(next,band[0]);riverTriangles_+=2;
            }
            previous=next;
        }
    }
    glEnd();glDisable(GL_POLYGON_OFFSET_FILL);glDisable(GL_TEXTURE_2D);
    if(view.waterTracers && moving) {
        glLineWidth(1.3f);glBegin(GL_LINES);
        for(const auto& [r,s]:riverSegments_) {
            const auto a=atlas->point(r,s,0),b=atlas->point(r,s,1);const double span=b.along-a.along;
            const double speed=(a.discharge+b.discharge)/(.8*(a.width+b.width)*(a.depth+b.depth)*.5);
            for(unsigned i=0;i<36;++i) {
                const auto seed=hash64((world.config().seed+hash64(r))^hash64(s+701+i));
                const double t=std::fmod(randomUnit(seed)+clock*speed/std::max(1.,span),1.);
                const auto p=atlas->point(r,s,t),behind=atlas->point(r,s,std::max(0.,t-std::min(2.,speed*1.5)/std::max(1.,span)));
                if(length(p.up-explorer.up())*radius>220 || p.level<=world.config().seaLevel+.02) continue;
                const double lateral=(randomUnit(seed+1)-.5)*.65;
                const auto head=local(normalized(p.up+p.left*(lateral*p.width/radius))*(radius+p.level+.025));
                const auto tail=local(normalized(behind.up+behind.left*(lateral*behind.width/radius))*(radius+behind.level+.025));
                glColor4f(.62f*static_cast<float>(light),.88f*static_cast<float>(light),.92f*static_cast<float>(light),.65f);glVertex3d(head.x,head.y,head.z);
                glColor4f(.62f*static_cast<float>(light),.88f*static_cast<float>(light),.92f*static_cast<float>(light),.05f);glVertex3d(tail.x,tail.y,tail.z);
            }
        }
        glEnd();
    }
    glPopAttrib();
}
void Renderer::drawGlobalWater(const World& world,const ViewState& view) {
    if(view.weatherLayer!=WeatherLayer::Rivers && view.weatherLayer!=WeatherLayer::Currents) return;
    if(waterMapDirty_ || waterMapLayer_!=view.weatherLayer) {
        glNewList(waterMap_,GL_COMPILE);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);glLineWidth(1.5f);
        const auto vertex=[](Vec3 p) {p=normalized(p)*1.004;glVertex3d(p.x,p.y,p.z);};
        const auto atlas=world.waterNetwork();
        if(view.weatherLayer==WeatherLayer::Rivers && world.modules().enabled(Module::Rivers)) {
            for(const auto& reach:atlas->reaches()) {
                const float amount=static_cast<float>(clamp(std::log1p(reach.points.back().discharge)/10,0.,1.));
                glColor3f(.08f,.38f+.35f*amount,.66f+.32f*amount);glBegin(GL_LINE_STRIP);
                for(std::size_t i=0;i<reach.points.size();i+=3) vertex(reach.points[i].up);
                vertex(reach.points.back().up);glEnd();
            }
        } else if(view.weatherLayer==WeatherLayer::Currents && world.modules().enabled(Module::Ocean) && world.modules().enabled(Module::WaterMotion) && world.modules().enabled(Module::Wind)) {
            glBegin(GL_LINES);
            for(std::size_t i=0;i<atlas->nodes().size();i+=3) {
                const auto p=atlas->nodes()[i].up,current=atlas->oceanCurrent(p);const double speed=length(current);if(speed<.004) continue;
                const auto forward=normalized(current),side=normalized(cross(p,forward));const double length=clamp(speed*.22,.020,.055);
                const auto tip=normalized(p+forward*length);glColor3f(.12f,static_cast<float>(.45+.45*clamp(speed/.2,0.,1.)),.92f);
                vertex(p);vertex(tip);vertex(tip);vertex(tip-forward*(length*.35)+side*(length*.22));vertex(tip);vertex(tip-forward*(length*.35)-side*(length*.22));
            }
            glEnd();
        }
        glLineWidth(1);glEndList();waterMapDirty_=false;waterMapLayer_=view.weatherLayer;
    }
    glCallList(waterMap_);
}
void Renderer::drawGlobalWeather(const GlobalWeather& weather,const ViewState& view) {
    if(!weather.enabled() || view.weatherLayer==WeatherLayer::Off || view.weatherLayer==WeatherLayer::Rivers || view.weatherLayer==WeatherLayer::Currents) return;
    if(weatherDirty_ || lastGlobalStep_!=weather.steps() || lastWeatherLayer_!=view.weatherLayer || lastGlobalClouds_!=weather.cloudsEnabled()) {
        glNewList(weatherMap_,GL_COMPILE);
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
        const auto emit=[&](Vec3 p) {
            const auto s=weather.sample(p);
            if(view.weatherLayer==WeatherLayer::Clouds) glColor4f(.88f,.94f,1,static_cast<float>(.84*(1-std::exp(-s.cloud*2))));
            else if(view.weatherLayer==WeatherLayer::Pressure) {
                const float t=static_cast<float>(clamp((s.pressure-94000)/10000,0.0,1.0));
                glColor4f(t,.25f+.45f*(1-std::abs(2*t-1)),1-t,.78f);
            } else {
                const float t=static_cast<float>(clamp(length(s.velocity)/70,0.0,1.0));
                glColor4f(std::min(1.0f,2*t),.35f+.5f*(1-t),1-t,.78f);
            }
            p=p*1.006;glVertex3d(p.x,p.y,p.z);
        };
        constexpr int nx=192,ny=96;
        for(int y=0;y<ny;++y) {
            glBegin(GL_TRIANGLE_STRIP);
            for(int x=0;x<=nx;++x) for(int dy:{1,0}) emit(direction(-pi/2+pi*(y+dy)/ny,-pi+tau*x/nx));
            glEnd();
        }
        glDepthMask(GL_TRUE);glDisable(GL_BLEND);glEndList();
        weatherDirty_=false;lastGlobalStep_=weather.steps();lastWeatherLayer_=view.weatherLayer;lastGlobalClouds_=weather.cloudsEnabled();
    }
    glCallList(weatherMap_);
    // Symbols mark systems; their screen radius is not the physical storm radius.
    glLineWidth(2);
    for(const auto& s:weather.storms()) if(s.deficit>1) {
        if(s.kind==StormKind::Hurricane) glColor3f(1,.57f,.16f);
        else if(s.kind==StormKind::Thunderstorm) glColor3f(.91f,.38f,1);
        else glColor3f(.25f,.94f,1);
        glBegin(GL_LINE_LOOP);
        for(int i=0;i<24;++i) {
            const double a=tau*i/24;const auto p=normalized(s.center+(east(s.center)*std::cos(a)+north(s.center)*std::sin(a))*.018)*1.012;
            glVertex3d(p.x,p.y,p.z);
        }
        glEnd();++stormMarkerCount_;
    }
    glLineWidth(1);
}
void Renderer::drawWeather(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight) {
    DrawTimer timer{timings_.weather};
    const auto& weather=simulation.weather;
    if(!weather.windEnabled()) return;
    const auto local=[&](Vec3 p) { return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)}; };
    const Vec3 origin=anchor_*world.config().radius;
    const Vec3 eye=explorer.up()*(world.config().radius+explorer.altitude());
    const Vec3 right=local(explorer.right()),up=local(normalized(cross(explorer.right(),explorer.forward())));
    const bool wire=view.surfaceStyle==SurfaceStyle::Wireframe;
    struct Sprite { Vec3 center; double distance,size; float alpha,shade; };
    std::vector<Sprite> sprites;
    if(weather.cloudsEnabled()) for(const auto& p:weather.clouds()) {
        const auto s=weather.sampleLocal(p.x,p.z);
        if(!s.available || s.cloudWater<0.004) continue;
        const Vec3 center=weather.position(p.x,p.z,weather.layerAltitude()+p.height);
        const double depth=dot(center-eye,explorer.forward());
        if(depth < -p.size || length(center-eye)>Weather::extent) continue;
        const double edge=1-smoothstep(Weather::extent*0.40,Weather::extent*0.50,std::max(std::abs(p.x),std::abs(p.z)));
        const float alpha=static_cast<float>((1-std::exp(-s.cloudWater*7))*edge*0.72);
        if(alpha<0.005f) continue;
        sprites.push_back({local(center-origin),depth,p.size,alpha,static_cast<float>(0.90+0.10*randomUnit(p.id+8))});
    }
    std::sort(sprites.begin(),sprites.end(),[](const auto& a,const auto& b) { return a.distance>b.distance; });
    cloudCount_=sprites.size();
    glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    if(!wire) glEnable(GL_FOG);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,cloudTexture_);
    glBegin(GL_QUADS);
    for(const auto& s:sprites) {
        if(wire) glColor4f(0.12f,0.46f,0.62f,s.alpha*0.70f);
        else glColor4f(lighting_.cloudColor.r*s.shade,lighting_.cloudColor.g*s.shade,lighting_.cloudColor.b*s.shade,s.alpha);
        const Vec3 horizontal=right*s.size,vertical=up*(s.size*0.68);
        const auto emit=[](Vec3 p,double u,double v) { glTexCoord2d(u,v); glVertex3d(p.x,p.y,p.z); };
        emit(s.center-horizontal-vertical,0,0); emit(s.center+horizontal-vertical,1,0);
        emit(s.center+horizontal+vertical,1,1); emit(s.center-horizontal+vertical,0,1);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
    const auto air=weather.sample(explorer.up());
    const double rain=air.rainRate*3600+simulation.rainExperiment();
    if(rain>.1) {
        const int count=static_cast<int>(clamp(rain*24,24.0,1800.0));
        const double time=std::remainder(view.seconds,100000.0);
        const auto wind=air.velocity*.7,vertical=explorer.up();
        const auto wrap=[](double x,double span) {return x-span*std::floor(x/span)-span/2;};
        glLineWidth(1);glBegin(GL_LINES);
        for(int i=0;i<count;++i) {
            const auto id=hash64(world.config().seed+static_cast<unsigned>(i)+78123);
            const double x=wrap(randomUnit(id)*120+dot(wind,east(explorer.up()))*time,120);
            const double z=wrap(randomUnit(id+1)*120+dot(wind,north(explorer.up()))*time,120);
            const double y=wrap(randomUnit(id+2)*60-7*time,60);
            const auto position=eye+east(explorer.up())*x+north(explorer.up())*z+vertical*y;
            const auto head=local(position-origin),tail=local(position-origin-(wind-vertical*7)*.08);
            const float light=static_cast<float>(.025+.975*daylight);
            glColor4f(.62f*light,.79f*light,.92f*light,.43f);glVertex3d(head.x,head.y,head.z);
            glColor4f(.7f*light,.84f*light,light,.06f);glVertex3d(tail.x,tail.y,tail.z);++rainCount_;
        }
        glEnd();
    }
    const double flash=simulation.global.flash(explorer.up());
    if(flash>0) {
        glDisable(GL_FOG);glLineWidth(3);glColor4f(.82f,.88f,1,static_cast<float>(std::min(1.0,flash*2)));
        for(const auto& event:simulation.global.lightning()) {
            if(length(event.direction-explorer.up())*world.config().radius>25000) continue;
            const auto strike=event.direction;
            const double ground=world.surface(strike);glBegin(GL_LINE_STRIP);
            const auto tick=event.cycle;
            for(unsigned i=0;i<=18;++i) {
                const double height=3000*(1-static_cast<double>(i)/18),jitter=i==18 ? 0 : 110;
                const auto p=local(strike*(world.config().radius+ground+height)-origin+
                    east(strike)*((randomUnit(event.id+tick*31+i)-.5)*jitter)+north(strike)*((randomUnit(event.id+tick*31+i+91)-.5)*jitter));
                glVertex3d(p.x,p.y,p.z);if(i) ++lightningCount_;
            }
            glEnd();
        }
        glLineWidth(1);
    }
    if(view.windTracers) {
        glLineWidth(1.5f); glBegin(GL_LINES);
        for(const auto& p:weather.tracers()) {
            const auto s=weather.sampleLocal(p.x,p.z);
            const Vec3 worldPosition=weather.position(p.x,p.z,weather.groundAt(p.x,p.z)+p.height);
            const double distance=length(worldPosition-eye);
            const float alpha=static_cast<float>(0.75*(1-smoothstep(1000,1900,distance)));
            if(!s.available || alpha<=0) continue;
            const Vec3 head=local(worldPosition-origin);
            const Vec3 tail=head-local(s.velocity)*2.0;
            glColor4f(0.60f,0.88f,1.0f,alpha); glVertex3d(head.x,head.y,head.z);
            glColor4f(0.60f,0.88f,1.0f,0); glVertex3d(tail.x,tail.y,tail.z);
        }
        glEnd(); glLineWidth(1);
    }
    glDisable(GL_FOG); glDisable(GL_BLEND); glDepthMask(GL_TRUE);
}
void Renderer::text(float x,float y,const std::string& value,Color color,bool large) {
    glColor3f(color.r,color.g,color.b); glRasterPos2f(x,y);
    glListBase((large ? headingFont_ : font_)-32);
    glCallLists(static_cast<GLsizei>(value.size()),GL_UNSIGNED_BYTE,value.data());
}
void Renderer::drawHud(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view) {
    const auto& weather=simulation.weather; const auto& water=simulation.water;
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_FOG);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,view.width,view.height,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const Color white{0.88f,0.93f,0.91f}, muted{0.53f,0.65f,0.65f}, mint{0.55f,0.91f,0.73f}, panel{0.025f,0.055f,0.064f};
    const float w=static_cast<float>(view.width), h=static_cast<float>(view.height);
    if(!view.globe && !explorer.underwater() && !explorer.underground())
        rect(0,0,w,h,{.75f,.82f,1},static_cast<float>(simulation.global.flash(explorer.up())*.15));
    if(!view.interfaceVisible) {glDisable(GL_BLEND);return;}
    rect(22,22,292,86,panel,0.9f); rect(22,22,3,86,mint);
    text(40,57,"PLANETLAB",white,true);
    text(41,80,"PROCEDURAL FIELD OBSERVATORY",muted);
    text(41,98,"SEED " + std::to_string(world.config().seed) + "  /  " + (view.globe ? "ORBIT" : "SURFACE"),mint);
    if(explorer.underground()) {
        rect(22,120,292,111,panel,0.88f);
        text(40,143,"UNDERGROUND / KARST",mint);
        text(40,165,"L headlamp: "+std::string(view.headlamp ? "ON" : "OFF"),white);
        if(const auto site=world.caveSite(explorer.up())) {
            const auto column=site->column(explorer.up());
            text(40,187,"CEILING "+fixed(column.ceiling-explorer.altitude(),1)+" m above eye",white);
        }
        text(40,215,"J next landmark / HOME landing",muted);
    } else if(explorer.underwater()) {
        const auto liquid=liquidAt(world,&water,&simulation.motion,explorer.up());
        const double depth=std::max(0.0,liquid.level-explorer.altitude());
        const double pressure=waterPressure(world.sample(explorer.up(),view.seconds).pressure,depth,liquid.ocean,world.config().gravity);
        rect(22,120,292,111,panel,0.88f);
        text(40,143,"UNDERWATER / SWIMMING",mint);
        text(40,165,"DEPTH "+fixed(depth,1)+" m   "+fixed(pressure/100000,2)+" bar",white);
        text(40,187,"BOTTOM "+fixed(explorer.altitude()-liquid.bed,1)+" m below eye",white);
        text(40,215,"SPACE up / CTRL dive / WASD swim",muted);
    } else if(weather.windEnabled()) {
        const auto air=weather.sample(explorer.up());
        const auto synoptic=simulation.global.sample(explorer.up());
        rect(22,120,292,111,panel,0.88f);
        text(40,143,synoptic.available && synoptic.severity>.15 ? std::string(stormName(synoptic.kind)) : "LOCAL WEATHER / "+fixed(weather.layerAltitude()/1000,1)+" km ASL",mint);
        text(40,165,"WIND "+fixed(length(air.velocity))+" m/s  E "+fixed(air.wind.east)+" N "+fixed(air.wind.north),white);
        text(40,187,weather.cloudsEnabled() ? "RH "+fixed(air.humidity*100,0)+"%  CLOUD "+fixed(air.cloudWater,2)+" kg/m2" : "MOISTURE / CLOUDS DISABLED",white);
        text(40,215,synoptic.available ? "SLP "+fixed(synoptic.pressure/100,0)+" hPa  RAIN "+fixed(air.rainRate*3600,1)+" mm/h" :
            (simulation.motion.windExperiment() ? "WATER WIND "+fixed(*simulation.motion.windExperiment(),0)+" m/s [EXPERIMENT]" : "V wind tracers: "+std::string(view.windTracers ? "ON" : "OFF")),muted);
    }
    const auto liquid=liquidAt(world,&water,&simulation.motion,explorer.up());
    const auto river=world.modules().enabled(Module::Rivers) ? world.waterNetwork()->sample(explorer.up()) : RiverSample{};
    if(view.globe) {
        rect(22,238,292,120,panel,.90f);
        text(40,259,"PLANET MAP / "+std::string(weatherLayerName(view.weatherLayer)),mint);
        const bool waterLayer=view.weatherLayer==WeatherLayer::Rivers || view.weatherLayer==WeatherLayer::Currents;
        text(40,280,waterLayer ? "REFERENCE CLIMATE / STEADY FLOW" : simulation.global.enabled() ? "GLOBAL +"+fixed(simulation.global.elapsed()/3600,2)+" h / 5 degree grid" : "GLOBAL WEATHER DISABLED",white);
        text(40,301,view.weatherLayer==WeatherLayer::Pressure ? "BLUE 940 -- RED 1040 hPa [SLP]" :
            (view.weatherLayer==WeatherLayer::Wind ? "BLUE 0 -- RED 70 m/s [10 m]" : waterLayer ? "CYAN: rivers / current direction" : "WHITE: column cloud / subgrid"),white);
        text(40,323,waterLayer ? "F7 visit rivers / TAB surface" : "CYAN cyclone / ORANGE tropical",white);
        text(40,346,"F3: weather / rivers / currents",muted);
    } else if(view.faunaInfo) {
        rect(22,241,292,117,panel,.88f);
        auto deer=simulation.fauna.visible(explorer.up());
        std::sort(deer.begin(),deer.end(),[&](const auto& a,const auto& b){return length(a.up-explorer.up())<length(b.up-explorer.up());});
        text(40,264,"DEER / "+std::to_string(deer.size())+" nearby",mint);
        if(!deer.empty()) {
            const auto& d=deer.front();unsigned group=0;for(const auto& h:simulation.fauna.herds()) if(h.habitat.id==d.herd) group=static_cast<unsigned>(h.members.size());
            text(40,287,"HERD "+std::to_string(group)+" / "+std::string(deerActivityName(d.activity)),white);
            text(40,310,fixed(length(d.up-explorer.up())*world.config().radius,0)+" m away  "+fixed(d.speed,1)+" m/s",muted);
        } else text(40,289,"No deer in view range",muted);
        text(40,344,"F11 visit herd / E next panel",muted);
    } else if(view.floraInfo) {
        rect(22,238,292,120,panel,.90f);
        text(40,259,"FLORA / nearest; "+std::to_string(plantSpeciesCount_)+" kinds",mint);
        if(focusPlant_) {
            const auto& p=*focusPlant_;
            text(40,280,std::string(plantTraits(p.species).name)+" / "+std::string(stageName(p.plant.stage)),white);
            text(40,301,"AGE "+fixed(p.plant.ageDays/365.25,2)+" y  HEIGHT "+fixed(p.height,2)+" m",white);
            text(40,323,std::string(flowerPhaseName(p.flowers.phase))+" / "+std::to_string(floweringCount_)+" in bloom",white);
        } else text(40,284,"No visible plants nearby",muted);
        text(40,346,"BIO +"+fixed(simulation.flora.days(),1)+"d  Y: "+(simulation.flora.rate()==1 ? "1x" : fixed(simulation.flora.rate()/86400,0)+"d/s"),muted);
    } else if(river.channel) {
        rect(22,238,292,120,panel,.90f);
        text(40,261,"RIVER / REFERENCE RUNOFF",mint);
        text(40,283,"WIDTH "+fixed(river.width,1)+" m  Q "+fixed(river.discharge,1)+" m3/s",white);
        text(40,305,"DEPTH "+fixed(river.depth,2)+" m  FLOW "+fixed(river.speed,2)+" m/s",white);
        text(40,327,"F7 major / river / stream",white);
        text(40,351,"U current tracers / E plants",muted);
    } else if(liquid.ocean) {
        rect(22,238,292,120,panel,0.90f);
        text(40,261,"OCEAN / WIND AND WAVES",mint);
        text(40,283,"HS "+fixed(simulation.motion.significantHeight(),2)+" m   FLOW "+fixed(length(liquid.current),3)+" m/s",white);
        text(40,305,"WIND10 "+fixed(length(simulation.motion.forcingWind()),1)+" m/s "+(simulation.motion.windExperiment() ? "[EXPERIMENT]" : "[ESTIMATE]"),white);
        text(40,327,"N wind: natural / 8 / 16 m/s",white);
        text(40,351,"U current tracers / ESC settings",muted);
    } else if(water.enabled() && water.sample(explorer.up()).available) {
        const auto ground=water.sample(explorer.up());
        rect(22,238,292,120,panel,0.90f);
        text(40,261,"SURFACE WATER / 4 m CELLS",mint);
        text(40,283,"DEPTH "+fixed(ground.depth*100,1)+" cm  FLOW "+fixed(std::hypot(ground.velocity.east,ground.velocity.north),2)+" m/s",white);
        text(40,305,"SOIL "+fixed(ground.saturation*100,0)+"%  BED "+fixed(ground.bedChange*1000,2)+" mm",white);
        text(40,327,"RAIN "+fixed(weather.sample(explorer.up()).rainRate*3600+simulation.rainExperiment(),1)+" mm/h "+
            (simulation.rainExperiment()>0 ? "[EXPERIMENT]" : "[CLOUDS]"),white);
        text(40,351,"B rain 0/30/80 | U "+std::string(view.waterTracers ? "ON" : "OFF"),muted);
    }
    rect(w-288,22,266,57,panel,0.9f);
    text(w-272,45,"DAY " + fixed(view.seconds/86400,2) + "  |  " + (view.paused ? "PAUSED" : fixed(view.timeScale,0)+"x"),white);
    text(w-272,66,"R " + fixed(world.config().radius/1000,0) + " km   " + fixed(view.fps,0) + " fps",muted);
    const auto sample=world.sample(explorer.up(),view.seconds);
    rect(22,h-242,292,174,panel,0.88f);
    text(40,h-217,std::string(landformName(sample.landform))+" / TELEMETRY",mint);
    text(40,h-192,"LAT " + fixed(degrees(latitude(explorer.up())),3) + "  LON " + fixed(degrees(longitude(explorer.up())),3),white);
    if(explorer.underground()) {
        const auto site=world.caveSite(explorer.up());
        if(site) text(40,h-170,"FLOOR  "+fixed(site->column(explorer.up()).floor,1)+" m   Limestone",white);
    } else text(40,h-170,"GROUND  " + fixed(water.ground(world,explorer.up()),0) + " m   " + std::string(biomeName(sample.biome)),white);
    text(40,h-148,"EYE     " + fixed(explorer.altitude(),1) + " m ASL",white);
    if(world.modules().enabled(Module::Atmosphere)) {
        text(40,h-126,"SURFACE " + fixed(sample.temperature-273.15,1) + " C   " + fixed(sample.pressure/100,0) + " hPa",white);
        text(40,h-104,"AIR     " + fixed(sample.density,3) + " kg/m3",muted);
    } else text(40,h-126,"ATMOSPHERE DISABLED",muted);
    text(40,h-82,"RAIN    " + (world.modules().enabled(Module::Climate) ? fixed(sample.precipitation,0)+" mm/y (estimate)" : "--"),muted);
    rect(w-288,98,266,274,panel,0.90f);
    text(w-272,118,"SIMULATION MODULES",mint);
    for(unsigned i=0;i<moduleCount;++i) {
        const auto m=static_cast<Module>(i);
        const bool on=world.modules().enabled(m);
        const float y=139+12*static_cast<float>(i);
        text(w-272,y,std::string(moduleNames[i]),on?white:muted);
        text(w-90,y,on ? "ON" : (world.modules().requested(m) ? "WAIT" : "OFF"),on?mint:muted);
    }
    text(w-272,370,"BASE SPHERE  /  ALWAYS ACTIVE",muted);
    rect(22,h-51,w-44,29,panel,0.9f);
    text(35,h-31,view.globe ? "RMB drag: rotate   LMB: travel   TAB surface   ESC settings   CTRL+U interface" :
        (view.captured ? "ESC release mouse" : "CLICK look / ESC settings") + std::string("   WASD  F fly  TAB globe  CTRL+U interface"),white);
    if(view.help) {
        rect(w-358,h-232,336,165,panel,0.9f);
        text(w-340,h-208,explorer.flying ? "FLIGHT / "+fixed(explorer.flightSpeed,0)+" m/s" : (explorer.swimming() ? "SWIM / 1.6 m/s + CURRENT" : "WALK / 2.5 m/s"),mint);
        text(w-340,h-185,"SHIFT faster   SPACE jump / up",white);
        text(w-340,h-164,"CTRL down      WHEEL flight speed",white);
        text(w-340,h-143,"+/- time  P pause  CTRL+S seed",white);
        text(w-340,h-122,"ESC settings / HOME landing",white);
        text(w-340,h-101,"F7 rivers  F8 meadow  X grove",muted);
        text(w-340,h-80,"F9 lakes  F10 coasts  F11 deer",mint);
    }
    if(!view.globe) {
        const float cx=w/2,cy=h/2;
        rect(cx-7,cy,5,1,white,0.7f); rect(cx+3,cy,5,1,white,0.7f);
        rect(cx,cy-7,1,5,white,0.7f); rect(cx,cy+3,1,5,white,0.7f);
        text(cx-50,38,"AZ " + fixed(std::fmod(degrees(explorer.heading())+360,360),0) + " deg",white);
    }
    text(w/2-80,60,!world.modules().enabled(Module::Sound) ? "SOUND OFF" :
        (!view.audioAvailable ? "SOUND / NO DEVICE" : "SOUND "+fixed(view.volume*100,0)+"%"),muted);
    if(view.shadowsEnabled && !shadowSupport()) text(w/2-115,80,"SHADOWS: NO STENCIL BUFFER",muted);
    glDisable(GL_BLEND);
}
bool globeHudHit(const ViewState& view,int x,int y) {
    if(!view.interfaceVisible) return false;
    const auto inside=[&](int left,int top,int width,int height) {return x>=left && x<left+width && y>=top && y<top+height;};
    return inside(22,22,292,86) || inside(22,120,292,238) || inside(22,view.height-242,292,174) ||
        inside(view.width-288,22,266,58) || inside(view.width-288,98,266,274) ||
        inside(view.width/2-90,45,180,20) ||
        inside(22,view.height-51,view.width-44,29) || (view.help && inside(view.width-358,view.height-232,336,165));
}
void Renderer::capture(const std::filesystem::path& path,int width,int height) const {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width*height*3));
    glPixelStorei(GL_PACK_ALIGNMENT,1); glReadBuffer(GL_BACK);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream out(path,std::ios::binary);
    if(!out) throw std::runtime_error("Cannot open screenshot");
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for(int y=height-1;y>=0;--y) out.write(reinterpret_cast<const char*>(pixels.data()+static_cast<std::size_t>(y*width*3)),width*3);
    if(!out) throw std::runtime_error("Cannot write screenshot");
}
} // namespace planet

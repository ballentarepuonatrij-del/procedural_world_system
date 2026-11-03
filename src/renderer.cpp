#include "planet/renderer.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace planet {
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
double terrainSpacing(const World& world,const Explorer& explorer) {
    const double aboveGround=std::max(0.0,explorer.altitude()-world.surface(explorer.up()));
    const int level=clamp(static_cast<int>(std::floor(std::log2(std::max(1.0,aboveGround/64)))),0,12);
    return std::ldexp(2.0,level);
}
std::vector<double> terrainAxis(double center,double step) {
    std::vector<double> points;
    // Nested powers-of-two lattices in a fixed cube-face chart. The sample
    // positions stay on the planet as the camera moves; only LOD membership changes.
    for(double spacing=step,extent=step*8;extent<=262144;spacing*=2,extent*=2) {
        const auto first=static_cast<std::int64_t>(std::ceil((center-extent)/spacing));
        const auto last=static_cast<std::int64_t>(std::floor((center+extent)/spacing));
        for(auto i=first;i<=last;++i) {
            const double position=static_cast<double>(i)*spacing;
            if(spacing==step || std::abs(position-center)>extent/2) points.push_back(position);
        }
    }
    std::sort(points.begin(),points.end());
    points.erase(std::unique(points.begin(),points.end()),points.end());
    return points;
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
void detailMesh(const SurfaceObject& object,Vec3 origin,Vec3 up,Vec3 right,bool simple) {
    right=rotate(right,up,object.angle);
    const Vec3 back=cross(right,up);
    const auto transform=[&](Vec3 p) { return origin+right*p.x+up*p.y+back*p.z; };
    const auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,Color color) {
        a=transform(a); b=transform(b); c=transform(c);
        const Vec3 n=normalized(cross(b-a,c-a));
        glNormal3d(n.x,n.y,n.z); glColor3f(color.r,color.g,color.b);
        glVertex3d(a.x,a.y,a.z); glVertex3d(b.x,b.y,b.z); glVertex3d(c.x,c.y,c.z);
    };
    const auto tube=[&](Vec3 a,Vec3 b,double base,double tip,Color color) {
        const Vec3 axis=normalized(b-a);
        const Vec3 side=normalized(cross(axis,Vec3{0.4,0.1,1})), front=cross(axis,side);
        for(int i=0;i<6;++i) {
            const Vec3 u=side*std::cos(tau*i/6)+front*std::sin(tau*i/6);
            const Vec3 v=side*std::cos(tau*(i+1)/6)+front*std::sin(tau*(i+1)/6);
            triangle(a+u*base,a+v*base,b+v*tip,color);
            triangle(a+u*base,b+v*tip,b+u*tip,color);
        }
    };
    const auto crown=[&](Vec3 center,Vec3 scale,Color color,std::uint64_t salt) {
        const int rings=simple ? 3 : 4, sides=simple ? 6 : 8;
        const auto vertex=[&](int row,int column) {
            column%=sides;
            const double lat=-pi/2+pi*row/rings, angle=tau*column/sides;
            const double rough=0.82+0.30*randomUnit(object.id+salt+static_cast<std::uint64_t>(row*17+column));
            return center+Vec3{std::cos(lat)*std::cos(angle)*scale.x*rough,
                               std::sin(lat)*scale.y,std::cos(lat)*std::sin(angle)*scale.z*rough};
        };
        for(int row=0;row<rings;++row) for(int column=0;column<sides;++column) {
            const float tint=static_cast<float>(0.83+0.22*randomUnit(object.id+salt+static_cast<std::uint64_t>(row*31+column)));
            const Color shaded{color.r*tint,color.g*tint,color.b*tint};
            const auto a=vertex(row,column),b=vertex(row,column+1),c=vertex(row+1,column),d=vertex(row+1,column+1);
            if(row>0) triangle(a,c,b,shaded);
            if(row+1<rings) triangle(b,c,d,shaded);
        }
    };
    const double h=object.height,r=object.radius;
    const float variation=static_cast<float>(0.85+0.30*randomUnit(object.id+11));
    const Color leaves{0.22f*variation,0.39f*variation,0.12f*variation};
    switch(object.kind) {
        case DetailKind::Rock:
            crown({0,h*0.32,0},{r,h*0.68,r*0.82},{0.46f*variation,0.43f*variation,0.36f*variation},101);
            break;
        case DetailKind::Tree: {
            const Color bark{0.27f,0.19f,0.12f};
            tube({0,-0.4,0},{0,h*0.72,0},h*0.025,h*0.008,bark);
            if(object.evergreen) {
                for(int layer=0;layer<3;++layer) {
                    const double base=h*(0.24+layer*0.20), width=r*(1-layer*0.22);
                    for(int i=0;i<8;++i) {
                        const Vec3 a{width*std::cos(tau*i/8),base,width*std::sin(tau*i/8)};
                        const Vec3 b{width*std::cos(tau*(i+1)/8),base,width*std::sin(tau*(i+1)/8)};
                        triangle(a,{0,std::min(h,base+h*0.4),0},b,{0.13f,0.29f,0.18f});
                    }
                }
            } else {
                crown({0,h*0.73,0},{r,h*0.27,r},leaves,200);
                if(!simple) for(int i=0;i<3;++i) {
                    const double angle=tau*i/3+0.4;
                    const Vec3 branch{std::cos(angle)*r*0.65,h*(0.56+i*0.055),std::sin(angle)*r*0.65};
                    tube({0,h*0.38,0},branch,h*0.012,h*0.004,bark);
                    crown(branch,{r*0.62,h*0.17,r*0.62},leaves,static_cast<std::uint64_t>(300+i*80));
                }
            }
            break;
        }
        case DetailKind::Shrub:
            crown({0,h*0.43,0},{r,h*0.57,r},leaves,500);
            break;
        case DetailKind::Grass:
            for(int i=0;i<5;++i) {
                const double angle=tau*i/5;
                const Vec3 side{std::cos(angle),0,std::sin(angle)};
                const Vec3 base=side*(r*0.35);
                triangle(base-side*0.06,base+side*0.06,base+Vec3{r*0.3,h*(0.6+0.1*i),r*0.2},leaves);
            }
            break;
    }
}
}
Renderer::Renderer(HDC dc) {
    font_=makeFont(dc,15,FW_NORMAL); headingFont_=makeFont(dc,28,FW_BOLD);
    terrain_=glGenLists(1); terrainGrid_=glGenLists(1); globe_=glGenLists(1); objects_=glGenLists(1);
    if (!terrain_ || !terrainGrid_ || !globe_ || !objects_) throw std::runtime_error("Cannot allocate terrain display lists");
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
    glDeleteLists(terrain_,1); glDeleteLists(globe_,1);
    glDeleteLists(terrainGrid_,1);
    glDeleteLists(objects_,1);
    glDeleteLists(font_,96); glDeleteLists(headingFont_,96);
    glDeleteTextures(1,&detailTexture_);
    glDeleteTextures(1,&cloudTexture_);
}
std::string Renderer::device() const {
    const auto* vendor=glGetString(GL_RENDERER);
    return vendor ? reinterpret_cast<const char*>(vendor) : "unknown";
}
void Renderer::buildTerrain(const World& world,const Explorer& explorer,double seconds) {
    // Local +Z points south so the rendering basis is right-handed.
    anchor_=explorer.up(); east_=east(anchor_); north_=-north(anchor_);
    const double radius=world.config().radius;
    terrainStep_=terrainSpacing(world,explorer);
    terrainEyeHeight_=explorer.altitude();
    Vec3 face;
    if(std::abs(anchor_.x)>=std::abs(anchor_.y) && std::abs(anchor_.x)>=std::abs(anchor_.z))
        face={std::copysign(1.0,anchor_.x),0,0};
    else if(std::abs(anchor_.y)>=std::abs(anchor_.z)) face={0,std::copysign(1.0,anchor_.y),0};
    else face={0,0,std::copysign(1.0,anchor_.z)};
    const Vec3 reference=std::abs(face.y)<0.5 ? Vec3{0,1,0} : Vec3{0,0,1};
    const Vec3 right=normalized(cross(reference,face)), down=cross(right,face);
    const double projection=radius/dot(anchor_,face);
    const auto xs=terrainAxis(dot(anchor_,right)*projection,terrainStep_);
    const auto ys=terrainAxis(dot(anchor_,down)*projection,terrainStep_);
    const int columns=static_cast<int>(xs.size()),rows=static_cast<int>(ys.size());
    const double aboveGround=std::max(0.0,explorer.altitude()-world.surface(anchor_));
    struct Vertex { Vec3 position; Color color; double u,v; float gridAlpha; Vec3 normal{}; bool ocean{}; };
    std::vector<Vertex> vertices(xs.size()*ys.size());
    const Noise material(world.config().seed^0x695c17ULL);
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        const Vec3 p=normalized(face*radius+right*xs[static_cast<std::size_t>(x)]+down*ys[static_cast<std::size_t>(y)]);
        const auto sample=world.sample(p,seconds);
        const double h=sample.ocean ? world.config().seaLevel : sample.elevation;
        const Vec3 relative=p*(radius+h)-anchor_*radius;
        auto color=world.color(sample);
        if(sample.ocean) {
            const float shallow=static_cast<float>(1-clamp((world.config().seaLevel-sample.elevation)/150,0.0,1.0));
            color.g+=0.10f*shallow; color.b+=0.07f*shallow;
        } else {
            const float variation=static_cast<float>(0.87+0.22*material.fbm(p*(radius/45),2));
            color.r*=variation; color.g*=variation; color.b*=variation;
        }
        // World-anchored coordinates keep the material stationary when the local mesh recentres.
        const Vec3 global=p*(radius+h);
        const Vec3 position{dot(relative,east_),dot(relative,anchor_),dot(relative,north_)};
        const float alpha=static_cast<float>(1-smoothstep(1500+aboveGround*4,10000+aboveGround*12,std::hypot(position.x,position.z)));
        vertices[static_cast<std::size_t>(y*columns+x)]={position,color,
            (global.x+global.z)*0.25,(global.y+global.z*0.37)*0.25,alpha,{},sample.ocean};
    }
    const auto at=[&](int x,int y)->Vertex& { return vertices[static_cast<std::size_t>(y*columns+x)]; };
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        auto& v=at(x,y);
        v.normal=normalized(cross(at(std::min(x+1,columns-1),y).position-at(std::max(x-1,0),y).position,
                                  at(x,std::max(y-1,0)).position-at(x,std::min(y+1,rows-1)).position));
        if(!v.ocean) {
            const float rock=static_cast<float>(smoothstep(0.12,0.42,1-v.normal.y));
            v.color={static_cast<float>(mix(v.color.r,0.44,rock)),static_cast<float>(mix(v.color.g,0.42,rock)),
                     static_cast<float>(mix(v.color.b,0.36,rock))};
        }
        // Grazing lines cover subpixel cells near the horizon. Fade them instead
        // of accumulating a bright, flickering band of overlapping edges.
        const double incidence=dot(v.normal,normalized(Vec3{0,terrainEyeHeight_,0}-v.position));
        v.gridAlpha*=static_cast<float>(smoothstep(0.0,0.08,incidence));
    }
    glNewList(terrain_,GL_COMPILE);
    const auto emit=[&](int x,int y) {
        const auto& v=at(x,y);
        glNormal3d(v.normal.x,v.normal.y,v.normal.z); glColor3f(v.color.r,v.color.g,v.color.b);
        glTexCoord2d(v.u,v.v);
        glVertex3d(v.position.x,v.position.y,v.position.z);
    };
    for(int y=0;y<rows-1;++y) {
        glBegin(GL_TRIANGLE_STRIP);
        for(int x=0;x<columns;++x) { emit(x,y+1); emit(x,y); }
        glEnd();
    }
    glEndList();
    glNewList(terrainGrid_,GL_COMPILE);
    glBegin(GL_LINES);
    const auto edge=[&](int ax,int ay,int bx,int by,bool diagonal) {
        const auto& a=vertices[static_cast<std::size_t>(ay*columns+ax)];
        const auto& b=vertices[static_cast<std::size_t>(by*columns+bx)];
        if(a.gridAlpha==0 && b.gridAlpha==0) return;
        const auto vertex=[&](const Vertex& v) {
            glColor4f(0.20f,0.82f,1.0f,v.gridAlpha*(diagonal ? 0.34f : 0.75f));
            glVertex3d(v.position.x,v.position.y,v.position.z);
        };
        vertex(a); vertex(b);
    };
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        if(x+1<columns) edge(x,y,x+1,y,false);
        if(y+1<rows) edge(x,y,x,y+1,false);
        // Match the diagonal of the filled triangle strip, including its depth.
        if(x+1<columns && y+1<rows) edge(x,y,x+1,y+1,true);
    }
    glEnd(); glEndList();
    glNewList(objects_,GL_COMPILE);
    detailCount_=0;
    if(aboveGround<1800) {
        const auto objects=surfaceObjects(world,anchor_,420,aboveGround<100);
        const auto local=[&](Vec3 p) { return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)}; };
        glBegin(GL_TRIANGLES);
        for(const auto& object:objects) {
            const double distance=length(object.up-anchor_)*radius;
            const double range=object.kind==DetailKind::Tree ? 420 : (object.kind==DetailKind::Rock ? 220 : 80);
            if(distance>range) continue;
            // Seat objects on the actual rendered triangle. Distant LOD samples
            // can differ from the exact collision height; roots must not float.
            const double project=radius/dot(object.up,face);
            const double x=dot(object.up,right)*project,y=dot(object.up,down)*project;
            const int ix=clamp(static_cast<int>(std::upper_bound(xs.begin(),xs.end(),x)-xs.begin())-1,0,columns-2);
            const int iy=clamp(static_cast<int>(std::upper_bound(ys.begin(),ys.end(),y)-ys.begin())-1,0,rows-2);
            const double tx=clamp((x-xs[static_cast<std::size_t>(ix)])/(xs[static_cast<std::size_t>(ix+1)]-xs[static_cast<std::size_t>(ix)]),0.0,1.0);
            const double ty=clamp((y-ys[static_cast<std::size_t>(iy)])/(ys[static_cast<std::size_t>(iy+1)]-ys[static_cast<std::size_t>(iy)]),0.0,1.0);
            const Vec3 a=at(ix,iy).position,d=at(ix+1,iy+1).position;
            const Vec3 base=tx>=ty ? a*(1-tx)+at(ix+1,iy).position*(tx-ty)+d*ty
                                  : a*(1-ty)+at(ix,iy+1).position*(ty-tx)+d*tx;
            detailMesh(object,base,local(object.up),local(east(object.up)),distance>100 || aboveGround>100);
            ++detailCount_;
        }
        glEnd();
    }
    glEndList();
    lastTerrainTime_=seconds; dirty_=false;
}
void Renderer::buildGlobe(const World& world,double seconds) {
    constexpr int latitudes=100,longitudes=200;
    glNewList(globe_,GL_COMPILE);
    const auto emit=[&](int x,int y) {
        const Vec3 p=direction(-pi/2+pi*y/latitudes,tau*x/longitudes);
        const auto sample=world.sample(p,seconds);
        const auto color=world.color(sample);
        // True radius, without vertical exaggeration.
        const Vec3 v=p*(1+(sample.ocean ? world.config().seaLevel : sample.elevation)/world.config().radius);
        glColor3f(color.r,color.g,color.b); glNormal3d(p.x,p.y,p.z); glVertex3d(v.x,v.y,v.z);
    };
    for(int y=0;y<latitudes;++y) {
        glBegin(GL_TRIANGLE_STRIP);
        for(int x=0;x<=longitudes;++x) { emit(x,y+1); emit(x,y); }
        glEnd();
    }
    glEndList(); lastGlobeTime_=seconds; globeDirty_=false;
}
void Renderer::draw(const World& world,const Explorer& explorer,const Weather& weather,const ViewState& view) {
    cloudCount_=0;
    glViewport(0,0,view.width,view.height);
    const auto sun=orbit(world.config(),view.seconds);
    const double daylight=smoothstep(-0.10,0.25,dot(explorer.up(),sun.sunDirection));
    const bool atmosphere=world.modules().enabled(Module::Atmosphere);
    const bool wireframe=view.surfaceStyle==SurfaceStyle::Wireframe && !view.globe;
    if(wireframe) glClearColor(0.008f,0.014f,0.035f,1);
    else if(view.globe || !atmosphere) glClearColor(0.015f,0.023f,0.038f,1);
    else glClearColor(static_cast<float>(0.015+0.43*daylight),static_cast<float>(0.025+0.57*daylight),static_cast<float>(0.06+0.64*daylight),1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING); glDisable(GL_BLEND);
    if(view.globe) {
        if(globeDirty_ || std::abs(view.seconds-lastGlobeTime_)>3600) buildGlobe(world,view.seconds);
        perspective(radians(44),static_cast<double>(view.width)/view.height,0.1,20);
        camera(explorer.up()*3.25,-explorer.up(),north(explorer.up()));
        const GLfloat light[]{static_cast<float>(sun.sunDirection.x),static_cast<float>(sun.sunDirection.y),static_cast<float>(sun.sunDirection.z),0};
        glLightfv(GL_LIGHT0,GL_POSITION,light);
        const GLfloat ambient[]{0.12f,0.14f,0.16f,1};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
        glDisable(GL_FOG); glCallList(globe_); glDisable(GL_LIGHTING);
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
        if(dirty_ || length(explorer.up()-anchor_)*world.config().radius>std::max(12.0,terrainStep_*4) ||
           std::abs(explorer.altitude()-terrainEyeHeight_)>std::max(32.0,terrainStep_*4) ||
           terrainSpacing(world,explorer)!=terrainStep_ || std::abs(view.seconds-lastTerrainTime_)>900)
            buildTerrain(world,explorer,view.seconds);
        perspective(radians(72),static_cast<double>(view.width)/view.height,0.25,400000);
        const auto local=[&](Vec3 p) { return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)}; };
        const Vec3 eye=local(explorer.up()*(world.config().radius+explorer.altitude())-anchor_*world.config().radius);
        camera(eye,local(explorer.forward()),local(explorer.up()));
        const Vec3 localSun=local(sun.sunDirection);
        const GLfloat light[]{static_cast<float>(localSun.x),static_cast<float>(localSun.y),static_cast<float>(localSun.z),0};
        glLightfv(GL_LIGHT0,GL_POSITION,light);
        const GLfloat ambient[]{static_cast<float>(0.025+0.20*daylight),static_cast<float>(0.03+0.22*daylight),static_cast<float>(0.045+0.25*daylight),1};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
        if(atmosphere && !wireframe) {
            const GLfloat fog[]{static_cast<float>(0.08+0.47*daylight),static_cast<float>(0.1+0.53*daylight),static_cast<float>(0.15+0.56*daylight),1};
            glEnable(GL_FOG); glFogfv(GL_FOG_COLOR,fog); glFogi(GL_FOG_MODE,GL_EXP2);
            glFogf(GL_FOG_DENSITY,static_cast<float>(0.000025*std::exp(-std::max(explorer.altitude(),0.0)/8500)));
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
        glCallList(objects_);
        glDisable(GL_POLYGON_OFFSET_FILL); glEnable(GL_COLOR_MATERIAL);
        glDisable(GL_TEXTURE_2D); glDisable(GL_FOG); glDisable(GL_LIGHTING);
        if(showGrid) {
            glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_LINE_SMOOTH); glHint(GL_LINE_SMOOTH_HINT,GL_NICEST); glLineWidth(1.0f);
            glCallList(terrainGrid_);
            if(wireframe) {
                glDisable(GL_COLOR_MATERIAL); glEnable(GL_LIGHTING);
                const GLfloat black[]{0,0,0,1}, cyan[]{0.12f,0.64f,0.78f,1};
                glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE,black);
                glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,cyan);
                glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); glCallList(objects_);
                glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
                glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
                glEnable(GL_COLOR_MATERIAL); glDisable(GL_LIGHTING);
            }
            glDisable(GL_LINE_SMOOTH); glDisable(GL_BLEND);
            glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
        }
        // Angular solar disk: approximately 0.53 degrees at 1 AU.
        const Vec3 center=eye+localSun*180000;
        const Vec3 solarRight=normalized(cross(localSun,Vec3{0,1,0}));
        const Vec3 solarUp=cross(solarRight,localSun);
        glColor3f(1.0f,0.94f,0.72f);
        glBegin(GL_TRIANGLE_FAN); glVertex3d(center.x,center.y,center.z);
        for(int i=0;i<=32;++i) {
            const double angle=tau*i/32;
            const Vec3 v=center+(solarRight*std::cos(angle)+solarUp*std::sin(angle))*(835/sun.distanceAU);
            glVertex3d(v.x,v.y,v.z);
        }
        glEnd();
        drawWeather(world,explorer,weather,view,daylight);
    }
    drawHud(world,explorer,weather,view);
}
void Renderer::drawWeather(const World& world,const Explorer& explorer,const Weather& weather,const ViewState& view,double daylight) {
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
        const float light=static_cast<float>(0.10+0.90*daylight)*s.shade;
        if(wire) glColor4f(0.12f,0.46f,0.62f,s.alpha*0.70f);
        else glColor4f(light,light,static_cast<float>(0.13+0.87*daylight)*s.shade,s.alpha);
        const Vec3 horizontal=right*s.size,vertical=up*(s.size*0.68);
        const auto emit=[](Vec3 p,double u,double v) { glTexCoord2d(u,v); glVertex3d(p.x,p.y,p.z); };
        emit(s.center-horizontal-vertical,0,0); emit(s.center+horizontal-vertical,1,0);
        emit(s.center+horizontal+vertical,1,1); emit(s.center-horizontal+vertical,0,1);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
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
void Renderer::drawHud(const World& world,const Explorer& explorer,const Weather& weather,const ViewState& view) {
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_FOG);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,view.width,view.height,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const Color white{0.88f,0.93f,0.91f}, muted{0.53f,0.65f,0.65f}, mint{0.55f,0.91f,0.73f}, panel{0.025f,0.055f,0.064f};
    const float w=static_cast<float>(view.width), h=static_cast<float>(view.height);
    rect(22,22,292,86,panel,0.9f); rect(22,22,3,86,mint);
    text(40,57,"PLANETLAB",white,true);
    text(41,80,"PROCEDURAL FIELD OBSERVATORY",muted);
    text(41,98,"SEED " + std::to_string(world.config().seed) + "  /  " + (view.globe ? "ORBIT" : "SURFACE"),mint);
    if(weather.windEnabled()) {
        const auto air=weather.sample(explorer.up());
        rect(22,120,292,111,panel,0.88f);
        text(40,143,"LOCAL WEATHER / "+fixed(weather.layerAltitude()/1000,1)+" km ASL",mint);
        text(40,165,"WIND "+fixed(length(air.velocity))+" m/s  E "+fixed(air.wind.east)+" N "+fixed(air.wind.north),white);
        text(40,187,weather.cloudsEnabled() ? "RH "+fixed(air.humidity*100,0)+"%  CLOUD "+fixed(air.cloudWater,2)+" kg/m2" : "MOISTURE / CLOUDS DISABLED",white);
        text(40,215,"V wind tracers: "+std::string(view.windTracers ? "ON" : "OFF"),muted);
    }
    rect(w-288,22,266,57,panel,0.9f);
    text(w-272,45,"DAY " + fixed(view.seconds/86400,2) + "  |  " + (view.paused ? "PAUSED" : fixed(view.timeScale,0)+"x"),white);
    text(w-272,66,"R " + fixed(world.config().radius/1000,0) + " km   " + fixed(view.fps,0) + " fps",muted);
    const auto sample=world.sample(explorer.up(),view.seconds);
    rect(22,h-242,292,174,panel,0.88f);
    text(40,h-217,"SURFACE TELEMETRY",mint);
    text(40,h-192,"LAT " + fixed(degrees(latitude(explorer.up())),3) + "  LON " + fixed(degrees(longitude(explorer.up())),3),white);
    text(40,h-170,"GROUND  " + fixed(sample.elevation,0) + " m   " + std::string(biomeName(sample.biome)),white);
    text(40,h-148,"EYE     " + fixed(explorer.altitude(),1) + " m ASL",white);
    if(world.modules().enabled(Module::Atmosphere)) {
        text(40,h-126,"SURFACE " + fixed(sample.temperature-273.15,1) + " C   " + fixed(sample.pressure/100,0) + " hPa",white);
        text(40,h-104,"AIR     " + fixed(sample.density,3) + " kg/m3",muted);
    } else text(40,h-126,"ATMOSPHERE DISABLED",muted);
    text(40,h-82,"RAIN    " + (world.modules().enabled(Module::Climate) ? fixed(sample.precipitation,0)+" mm/y (estimate)" : "--"),muted);
    rect(w-288,98,266,288,panel,0.90f);
    text(w-272,123,"SIMULATION MODULES",mint);
    for(unsigned i=0;i<moduleCount;++i) {
        const auto m=static_cast<Module>(i);
        const bool on=world.modules().enabled(m);
        const float y=151+27*static_cast<float>(i);
        text(w-272,y,"["+std::to_string(i+1)+"] "+std::string(moduleNames[i]),on?white:muted);
        text(w-90,y,on ? "ON" : (world.modules().requested(m) ? "WAIT" : "OFF"),on?mint:muted);
    }
    text(w-272,373,"BASE SPHERE  /  ALWAYS ACTIVE",muted);
    rect(22,h-51,w-44,29,panel,0.9f);
    text(35,h-31,(view.captured ? "ESC release mouse" : "CLICK to look") + std::string("   WASD move   F fly   G grid   TAB orbit   H help   T noon   R new seed"),white);
    if(view.help) {
        rect(w-358,h-232,336,165,panel,0.9f);
        text(w-340,h-208,explorer.flying ? "FLIGHT / "+fixed(explorer.flightSpeed,0)+" m/s" : "WALK / 2.5 m/s",mint);
        text(w-340,h-185,"SHIFT faster   SPACE jump / up",white);
        text(w-340,h-164,"CTRL down      WHEEL flight speed",white);
        text(w-340,h-143,"+ / - time     P pause",white);
        text(w-340,h-122,"HOME landing   1-8 toggle modules",white);
        text(w-340,h-101,"G surface: "+std::string(surfaceStyleName(view.surfaceStyle)),mint);
        text(w-340,h-80,"TAB switches surface / globe",muted);
    }
    if(!view.globe) {
        const float cx=w/2,cy=h/2;
        rect(cx-7,cy,5,1,white,0.7f); rect(cx+3,cy,5,1,white,0.7f);
        rect(cx,cy-7,1,5,white,0.7f); rect(cx,cy+3,1,5,white,0.7f);
        text(cx-50,38,"AZ " + fixed(std::fmod(degrees(explorer.heading())+360,360),0) + " deg",white);
    }
    glDisable(GL_BLEND);
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

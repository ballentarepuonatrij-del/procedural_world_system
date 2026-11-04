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
    const auto& traits=plantTraits(object.species);
    const float foliage=static_cast<float>(object.plant.foliage);
    const Color leaves{(traits.leaves.r*foliage+.38f*(1-foliage))*variation,
        (traits.leaves.g*foliage+.26f*(1-foliage))*variation,(traits.leaves.b*foliage+.12f*(1-foliage))*variation};
    if(traits.aquatic || object.species==Species::Fern) {
        if(object.species==Species::Kelp) {
            // A holdfast, flexible stipe and alternating broad blades.
            tube({0,0,0},{r*.4,h,0},h*.009,h*.003,{.34f,.29f,.10f});
            for(int i=1;i<9;++i) {
                const double y=h*i/10,side=i%2 ? 1.0 : -1.0;
                const Vec3 a{r*.4*i/10,y,0},b=a+Vec3{r*side,y*.06,r*.5};
                triangle(a,b+Vec3{0,h*.12,0},b+Vec3{r*side*.3,0,-r*.4},leaves);
                triangle(a,b+Vec3{r*side*.3,0,-r*.4},b-Vec3{0,h*.06,0},leaves);
            }
        } else if(object.species==Species::Fern) {
            for(int i=0;i<7;++i) {
                const Vec3 direction{std::cos(tau*i/7),0,std::sin(tau*i/7)};
                const Vec3 side=cross(direction,Vec3{0,1,0});
                for(int j=1;j<6;++j) {
                    const double f=j/6.0;
                    const Vec3 a=direction*(r*f)+Vec3{0,h*std::sin(f*pi*.65),0};
                    const Vec3 b=direction*(r*(f-.2))+Vec3{0,h*std::sin(std::max(0.0,f-.2)*pi*.65),0};
                    triangle(b,a+side*(r*.23*(1-f)),a,leaves);
                    triangle(b,a,a-side*(r*.23*(1-f)),leaves);
                }
            }
        } else {
            const int blades=object.species==Species::Rockweed ? 9 : 7;
            for(int i=0;i<blades;++i) {
                const Vec3 side{std::cos(tau*i/blades),0,std::sin(tau*i/blades)};
                for(int j=0;j<4;++j) {
                    const auto point=[&](double f) { return side*(r*(.2+.65*f*f))+Vec3{0,h*f*(.65+.35*randomUnit(object.id+static_cast<unsigned>(i))),0}; };
                    const double a=j/4.0,b=(j+1)/4.0;
                    const double width=r*(object.species==Species::Rockweed ? .19 : .07);
                    triangle(point(a)-side*width*(1-a),point(a)+side*width*(1-a),point(b)+side*width*(1-b),leaves);
                    triangle(point(a)-side*width*(1-a),point(b)+side*width*(1-b),point(b)-side*width*(1-b),leaves);
                }
            }
        }
        return;
    }
    switch(object.kind) {
        case DetailKind::Rock:
            crown({0,h*0.32,0},{r,h*0.68,r*0.82},{0.46f*variation,0.43f*variation,0.36f*variation},101);
            break;
        case DetailKind::Tree: {
            const Color bark=object.species==Species::Birch ? Color{.78f,.77f,.69f} : Color{.27f,.19f,.12f};
            tube({0,-0.4,0},{0,h*0.72,0},h*0.025,h*0.008,bark);
            if(object.plant.stage==GrowthStage::Dead) {
                for(int i=0;i<3;++i) tube({0,h*.4,0},{r*std::cos(tau*i/3),h*.75,r*std::sin(tau*i/3)},h*.014,h*.003,bark);
                break;
            }
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
                const bool flat=object.species==Species::Acacia;
                crown({0,h*(flat ? .86 : .73),0},{r,h*(flat ? .14 : .27),r},leaves,200);
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
            if(object.species==Species::Heather && foliage>.5f && object.plant.stage==GrowthStage::Mature)
                for(int i=0;i<3;++i) crown({r*.6*std::cos(tau*i/3),h*.8,r*.6*std::sin(tau*i/3)},{r*.24,h*.18,r*.24},{.61f,.32f,.54f},static_cast<unsigned>(700+i));
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
    terrain_=glGenLists(1); terrainGrid_=glGenLists(1); globe_=glGenLists(1); objects_=glGenLists(1); caves_=glGenLists(1);
    if (!terrain_ || !terrainGrid_ || !globe_ || !objects_ || !caves_) throw std::runtime_error("Cannot allocate terrain display lists");
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
    glDeleteLists(objects_,1); glDeleteLists(caves_,1);
    glDeleteLists(font_,96); glDeleteLists(headingFont_,96);
    glDeleteTextures(1,&detailTexture_);
    glDeleteTextures(1,&cloudTexture_);
}
std::string Renderer::device() const {
    const auto* vendor=glGetString(GL_RENDERER);
    return vendor ? reinterpret_cast<const char*>(vendor) : "unknown";
}
void Renderer::buildTerrain(const World& world,const Explorer& explorer,const Hydrology& water,double seconds) {
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
    const auto cave=world.caveSite(anchor_);
    const double projection=radius/dot(anchor_,face);
    auto xs=terrainAxis(dot(anchor_,right)*projection,terrainStep_);
    auto ys=terrainAxis(dot(anchor_,down)*projection,terrainStep_);
    if(water.enabled() && explorer.altitude()-water.ground(world,anchor_)<1200) {
        double minX=1e30,maxX=-1e30,minY=1e30,maxY=-1e30;
        for(int z:{0,Hydrology::resolution-1}) for(int x:{0,Hydrology::resolution-1}) {
            const auto q=water.directionAt(water.cellX(x),water.cellZ(z));
            const double k=radius/dot(q,face),cx=dot(q,right)*k,cy=dot(q,down)*k;
            minX=std::min(minX,cx); maxX=std::max(maxX,cx); minY=std::min(minY,cy); maxY=std::max(maxY,cy);
        }
        const auto refine=[](std::vector<double>& axis,double low,double high) {
            for(double v=std::ceil(low/4)*4;v<=high;v+=4) axis.push_back(v);
            std::sort(axis.begin(),axis.end()); axis.erase(std::unique(axis.begin(),axis.end()),axis.end());
        };
        refine(xs,minX,maxX); refine(ys,minY,maxY);
    }
    if(cave && explorer.altitude()-world.surface(anchor_)<1200) {
        double minX=1e30,maxX=-1e30,minY=1e30,maxY=-1e30;
        for(double z:{-85.,290.}) for(double x:{-110.,110.}) {
            const auto q=cave->directionAt(x,z); const double k=radius/dot(q,face);
            minX=std::min(minX,dot(q,right)*k); maxX=std::max(maxX,dot(q,right)*k);
            minY=std::min(minY,dot(q,down)*k); maxY=std::max(maxY,dot(q,down)*k);
        }
        const double step=std::pow(2.0,std::ceil(std::log2(2/std::pow(dot(cave->anchor,face),2))));
        const auto refine=[&](std::vector<double>& axis,double low,double high) {
            for(double v=std::ceil(low/step)*step;v<=high;v+=step) axis.push_back(v);
            std::sort(axis.begin(),axis.end()); axis.erase(std::unique(axis.begin(),axis.end()),axis.end());
        };
        refine(xs,minX,maxX); refine(ys,minY,maxY);
    }
    const int columns=static_cast<int>(xs.size()),rows=static_cast<int>(ys.size());
    const double aboveGround=std::max(0.0,explorer.altitude()-world.surface(anchor_));
    struct Vertex { Vec3 position; Color color; double u,v; float gridAlpha; Vec3 normal{}; bool ocean{}; Vec3 unit{}; double height{}; CaveColumn cavity{}; Color rockColor{}; };
    std::vector<Vertex> vertices(xs.size()*ys.size());
    waterVertices_.clear(); waterTriangles_.clear();
    waterVertices_.reserve(vertices.size());
    const Noise material(world.config().seed^0x695c17ULL);
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        const Vec3 p=normalized(face*radius+right*xs[static_cast<std::size_t>(x)]+down*ys[static_cast<std::size_t>(y)]);
        const auto sample=world.sample(p,seconds);
        const auto ground=water.sample(p);
        const double original=sample.elevation;
        const double h=ground.available ? mix(original,ground.bed,ground.weight) : original;
        const Vec3 relative=p*(radius+h)-anchor_*radius;
        auto color=world.color(sample);
        if(sample.ocean) {
            const float shallow=static_cast<float>(1-clamp((world.config().seaLevel-sample.elevation)/150,0.0,1.0));
            color={0.13f+0.30f*shallow,0.19f+0.20f*shallow,0.20f+0.10f*shallow};
        } else {
            const float variation=static_cast<float>(0.87+0.22*material.fbm(p*(radius/45),2));
            const float moisture=static_cast<float>(ground.available ? 1-0.28*ground.saturation*ground.weight : 1);
            color.r*=variation*moisture; color.g*=variation*moisture; color.b*=variation*moisture;
        }
        // World-anchored coordinates keep the material stationary when the local mesh recentres.
        const Vec3 global=p*(radius+h);
        const Vec3 position{dot(relative,east_),dot(relative,anchor_),dot(relative,north_)};
        const double localSpacing=std::max(x>0 ? xs[static_cast<std::size_t>(x)]-xs[static_cast<std::size_t>(x-1)] : terrainStep_,
                                           y>0 ? ys[static_cast<std::size_t>(y)]-ys[static_cast<std::size_t>(y-1)] : terrainStep_);
        waterVertices_.push_back({p,position,h,world.modules().enabled(Module::Ocean) || (ground.available && ground.weight>0),localSpacing});
        const float alpha=static_cast<float>(1-smoothstep(1500+aboveGround*4,10000+aboveGround*12,std::hypot(position.x,position.z)));
        const double band=0.5+0.5*std::sin(h/7.5+0.35*std::sin(h/27));
        Color rock{0.44f,0.42f,0.36f};
        if(sample.landform==Landform::Canyon || sample.landform==Landform::Plateau)
            rock={static_cast<float>(0.52+0.14*band),static_cast<float>(0.30+0.12*band),static_cast<float>(0.18+0.10*band)};
        else if(sample.landform==Landform::Karst) rock={static_cast<float>(0.48+0.12*band),static_cast<float>(0.46+0.12*band),static_cast<float>(0.39+0.12*band)};
        else if(sample.landform==Landform::Mountains || sample.landform==Landform::Gorge) rock={0.39f,0.41f,0.42f};
        if(cave && cave->protectedGround(p)) color=rock;
        vertices[static_cast<std::size_t>(y*columns+x)]={position,color,
            (global.x+global.z)*0.25,(global.y+global.z*0.37)*0.25,alpha,{},sample.ocean,p,h,cave ? cave->column(p) : CaveColumn{},rock};
    }
    const auto at=[&](int x,int y)->Vertex& { return vertices[static_cast<std::size_t>(y*columns+x)]; };
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        auto& v=at(x,y);
        v.normal=normalized(cross(at(std::min(x+1,columns-1),y).position-at(std::max(x-1,0),y).position,
                                  at(x,std::max(y-1,0)).position-at(x,std::min(y+1,rows-1)).position));
        if(!v.ocean) {
            const float rock=static_cast<float>(smoothstep(0.12,0.42,1-v.normal.y));
            v.color={static_cast<float>(mix(v.color.r,v.rockColor.r,rock)),static_cast<float>(mix(v.color.g,v.rockColor.g,rock)),
                     static_cast<float>(mix(v.color.b,v.rockColor.b,rock))};
        }
        // Grazing lines cover subpixel cells near the horizon. Fade them instead
        // of accumulating a bright, flickering band of overlapping edges.
        const double incidence=dot(v.normal,normalized(Vec3{0,terrainEyeHeight_,0}-v.position));
        v.gridAlpha*=static_cast<float>(smoothstep(0.0,0.08,incidence));
    }
    for(int y=0;y<rows-1;++y) for(int x=0;x<columns-1;++x) {
        const auto a=static_cast<std::size_t>(y*columns+x),b=a+1,c=a+static_cast<std::size_t>(columns),d=c+1;
        for(const auto tri:{std::array<std::size_t,3>{a,c,d},std::array<std::size_t,3>{a,d,b}})
            if(waterVertices_[tri[0]].available && waterVertices_[tri[1]].available && waterVertices_[tri[2]].available) waterTriangles_.push_back(tri);
    }
    waterRevision_=water.revision(); waterSteps_=water.steps();
    const auto interpolate=[&](const Vertex& a,const Vertex& b,double t) {
        Vertex v=a; v.unit=normalized(a.unit*(1-t)+b.unit*t); v.height=world.elevation(v.unit);
        const Vec3 relative=v.unit*(radius+v.height)-anchor_*radius;
        v.position={dot(relative,east_),dot(relative,anchor_),dot(relative,north_)};
        v.normal=normalized(a.normal*(1-t)+b.normal*t);
        v.color={static_cast<float>(mix(a.color.r,b.color.r,t)),static_cast<float>(mix(a.color.g,b.color.g,t)),static_cast<float>(mix(a.color.b,b.color.b,t))};
        v.u=mix(a.u,b.u,t); v.v=mix(a.v,b.v,t); v.gridAlpha=static_cast<float>(mix(a.gridAlpha,b.gridAlpha,t));
        v.cavity=cave ? cave->column(v.unit) : CaveColumn{}; return v;
    };
    const auto field=[](const Vertex& v,int plane) {
        if(plane==0) return v.cavity.field;
        if(plane==1) return -std::min(v.cavity.ceiling-v.height,v.height-v.cavity.floor);
        return v.height-(plane==2 ? v.cavity.floor : v.cavity.ceiling);
    };
    const auto clip=[&](const std::vector<Vertex>& input,int plane) {
        std::vector<Vertex> output;
        if(input.empty()) return output;
        for(std::size_t i=0;i<input.size();++i) {
            const auto& a=input[i]; const auto& b=input[(i+1)%input.size()];
            const bool inside=field(a,plane)>=0,next=field(b,plane)>=0;
            if(inside) output.push_back(a);
            if(inside!=next) {
                double lo=0,hi=1;
                for(int j=0;j<14;++j) { const double mid=(lo+hi)/2; if((field(interpolate(a,b,mid),plane)>=0)==inside) lo=mid; else hi=mid; }
                auto v=interpolate(a,b,(lo+hi)/2);
                if(plane==0) { v.cavity.field=0; v.cavity.floor=v.cavity.ceiling=(v.cavity.floor+v.cavity.ceiling)/2; }
                else if(plane==2) v.cavity.floor=v.height;
                else if(plane==3) v.cavity.ceiling=v.height;
                output.push_back(v);
            }
        }
        return output;
    };
    const auto terrainVertex=[](const Vertex& v) {
        glNormal3d(v.normal.x,v.normal.y,v.normal.z); glColor3f(v.color.r,v.color.g,v.color.b);
        glTexCoord2d(v.u,v.v); glVertex3d(v.position.x,v.position.y,v.position.z);
    };
    glNewList(terrain_,GL_COMPILE); glBegin(GL_TRIANGLES);
    for(int y=0;y<rows-1;++y) for(int x=0;x<columns-1;++x) {
        for(const auto& tri:{std::array<Vertex,3>{at(x,y),at(x,y+1),at(x+1,y+1)},std::array<Vertex,3>{at(x,y),at(x+1,y+1),at(x+1,y)}}) {
            const auto polygon=cave ? clip({tri.begin(),tri.end()},1) : std::vector<Vertex>{tri.begin(),tri.end()};
            for(std::size_t i=1;i+1<polygon.size();++i) { terrainVertex(polygon[0]); terrainVertex(polygon[i]); terrainVertex(polygon[i+1]); }
        }
    }
    glEnd(); glEndList();
    caveCount_=0;
    glNewList(caves_,GL_COMPILE); glBegin(GL_TRIANGLES);
    if(cave) for(int y=0;y<rows-1;++y) for(int x=0;x<columns-1;++x) {
        for(const auto& tri:{std::array<Vertex,3>{at(x,y),at(x,y+1),at(x+1,y+1)},std::array<Vertex,3>{at(x,y),at(x+1,y+1),at(x+1,y)}}) {
            if(std::max({tri[0].cavity.field,tri[1].cavity.field,tri[2].cavity.field})<0) continue;
            const auto footprint=clip({tri.begin(),tri.end()},0);
            for(int side=2;side<=3;++side) {
                const auto polygon=clip(footprint,side);
                const auto point=[&](const Vertex& v) {
                    const double h=side==2 ? v.cavity.floor : v.cavity.ceiling;
                    const Vec3 p=v.unit*(radius+h)-anchor_*radius;
                    return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)};
                };
                for(std::size_t i=1;i+1<polygon.size();++i) {
                    const auto a=point(polygon[0]),b=point(polygon[i]),c=point(polygon[i+1]);
                    auto normal=normalized(cross(b-a,c-a));
                    if((normal.y>0)!=(side==2)) normal=-normal;
                    glNormal3d(normal.x,normal.y,normal.z);
                    for(const auto index:{std::size_t{0},i,i+1}) {
                        const auto& v=polygon[index]; const auto p=point(v);
                        const double height=side==2 ? v.cavity.floor : v.cavity.ceiling;
                        const float shade=static_cast<float>(0.83+0.12*std::sin(height*1.5)+0.05*std::sin(height*5));
                        glColor3f(0.51f*shade,0.47f*shade,0.38f*shade); glTexCoord2d(v.u,v.v);
                        glVertex3d(p.x,p.y,p.z);
                    }
                    ++caveCount_;
                }
            }
        }
    }
    glEnd(); glEndList();
    glNewList(terrainGrid_,GL_COMPILE);
    glBegin(GL_LINES);
    const auto edge=[&](int ax,int ay,int bx,int by,bool diagonal) {
        const auto& a=vertices[static_cast<std::size_t>(ay*columns+ax)];
        const auto& b=vertices[static_cast<std::size_t>(by*columns+bx)];
        if(a.gridAlpha==0 && b.gridAlpha==0) return;
        if(cave && (field(a,1)<0 || field(b,1)<0)) return;
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
    objectSites_.clear();lastFloraDay_=-1e30;
    if(aboveGround<1800) {
        const auto objects=surfaceSites(world,anchor_,420,aboveGround<100);
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
            objectSites_.push_back({object,base});
        }
    }
    lastTerrainTime_=seconds; dirty_=false;
}
void Renderer::buildObjects(const World& world,const Flora& flora) {
    glNewList(objects_,GL_COMPILE);glBegin(GL_TRIANGLES);
    detailCount_=0;plantCount_=0;marineCount_=0;plantHeightSum_=0;focusPlant_.reset();
    std::array<bool,speciesCount> species{};double nearest=1e30;
    const auto local=[&](Vec3 p) { return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)}; };
    for(const auto& site:objectSites_) {
        const auto object=grownObject(site.object,&flora);
        if(object.height<.015) continue;
        const double distance=length(object.up-anchor_)*world.config().radius;
        detailMesh(object,site.base,local(object.up),local(east(object.up)),distance>100);
        ++detailCount_;
        if(object.species!=Species::None) {
            ++plantCount_;species[static_cast<unsigned>(object.species)]=true;plantHeightSum_+=object.height;
            if(plantTraits(object.species).aquatic) ++marineCount_;
            if(distance<nearest) { nearest=distance;focusPlant_=object; }
        }
    }
    glEnd();glEndList();
    plantSpeciesCount_=static_cast<std::size_t>(std::count(species.begin(),species.end(),true));
    lastFloraDay_=flora.days();lastYoung_=flora.young();
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
void Renderer::draw(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view) {
    const auto& weather=simulation.weather; const auto& water=simulation.water;
    cloudCount_=0; waterCount_=0;
    glViewport(0,0,view.width,view.height);
    const auto sun=orbit(world.config(),view.seconds);
    const double daylight=smoothstep(-0.10,0.25,dot(explorer.up(),sun.sunDirection));
    const bool atmosphere=world.modules().enabled(Module::Atmosphere);
    const bool wireframe=view.surfaceStyle==SurfaceStyle::Wireframe && !view.globe;
    const auto liquid=liquidAt(world,&water,&simulation.motion,explorer.up());
    const bool submerged=explorer.underwater() && !view.globe;
    const double diveDepth=submerged ? std::max(0.0,liquid.level-explorer.altitude()) : 0;
    const double waterLight=std::exp(-0.045*diveDepth)*(0.15+0.85*daylight);
    const GLfloat waterFog[]{0.015f*static_cast<float>(waterLight),0.14f*static_cast<float>(waterLight),0.19f*static_cast<float>(waterLight),1};
    if(submerged) glClearColor(waterFog[0],waterFog[1],waterFog[2],1);
    else if(wireframe) glClearColor(0.008f,0.014f,0.035f,1);
    else if(view.globe || !atmosphere) glClearColor(0.015f,0.023f,0.038f,1);
    else glClearColor(static_cast<float>(0.015+0.43*daylight),static_cast<float>(0.025+0.57*daylight),static_cast<float>(0.06+0.64*daylight),1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING); glDisable(GL_BLEND); glDisable(GL_LIGHT1);
    const GLfloat sunColor[]{static_cast<float>(submerged ? 0.35*waterLight : 1),static_cast<float>(submerged ? 0.75*waterLight : 1),static_cast<float>(submerged ? waterLight : 1),1};
    glLightfv(GL_LIGHT0,GL_DIFFUSE,sunColor);
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
        if(dirty_ || waterRevision_!=water.revision() || (water.enabled() && water.steps()-waterSteps_>=150) || length(explorer.up()-anchor_)*world.config().radius>std::max(12.0,terrainStep_*4) ||
           std::abs(explorer.altitude()-terrainEyeHeight_)>std::max(32.0,terrainStep_*4) ||
           terrainSpacing(world,explorer)!=terrainStep_ || std::abs(view.seconds-lastTerrainTime_)>900)
            buildTerrain(world,explorer,water,view.seconds);
        if(std::abs(simulation.flora.days()-lastFloraDay_)>=.25 || simulation.flora.young()!=lastYoung_)
            buildObjects(world,simulation.flora);
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
        const GLfloat ambient[]{static_cast<float>(0.025+0.20*daylight),static_cast<float>(0.03+0.22*daylight),static_cast<float>(0.045+0.25*daylight),1};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
        if(submerged) {
            const GLfloat dim[]{0.02f,static_cast<float>(0.10*waterLight),static_cast<float>(0.15*waterLight),1}; glLightModelfv(GL_LIGHT_MODEL_AMBIENT,dim);
            glEnable(GL_FOG); glFogfv(GL_FOG_COLOR,waterFog); glFogi(GL_FOG_MODE,GL_EXP);
            glFogf(GL_FOG_DENSITY,static_cast<float>(0.045+std::min(0.3,liquid.sediment*0.01)));
        } else if(atmosphere && !wireframe) {
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
        // Leaves and marine blades are thin sheets, lit from either side.
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE);
        glCallList(objects_);
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_FALSE);
        if(caveCount_>0) {
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
            glDisable(GL_LIGHT1); glEnable(GL_LIGHT0); glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
        }
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
        if(!submerged) {
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
    }
    drawHud(world,explorer,simulation,view);
}
void Renderer::drawWater(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view,double daylight) {
    const auto& water=simulation.water; const auto& motion=simulation.motion;
    const bool oceanEnabled=world.modules().enabled(Module::Ocean);
    if(!water.enabled() && !oceanEnabled) return;
    const auto local=[&](Vec3 p) { return Vec3{dot(p,east_),dot(p,anchor_),dot(p,north_)}; };
    const double radius=world.config().radius,sea=world.config().seaLevel;
    const Vec3 origin=anchor_*radius,eye=explorer.up()*(radius+explorer.altitude());
    const Vec3 sun=orbit(world.config(),view.seconds).sunDirection;
    struct Vertex { Vec3 position; Color color; float alpha; double depth; };
    std::vector<Vertex> vertices(waterVertices_.size()),triangles;
    const bool wire=view.surfaceStyle==SurfaceStyle::Wireframe;
    if(!wire && world.modules().enabled(Module::Atmosphere)) glEnable(GL_FOG);
    for(std::size_t i=0;i<waterVertices_.size();++i) {
        const auto& v=waterVertices_[i]; if(!v.available) continue;
        const auto ground=water.sample(v.up);
        const bool ocean=oceanEnabled && (v.altitude<sea || !ground.available || ground.depth<0.0005);
        const double depth=ocean ? sea-v.altitude : (ground.available ? ground.depth*ground.weight : 0);
        double level=ocean ? sea : v.altitude+depth;
        const auto wave=motion.waves(v.up,std::max(0.0,depth),ocean,v.spacing*2);
        level+=wave.height;
        const Vec3 position=v.up*(radius+level);
        const Vec3 normal=normalized(v.up-wave.gradient),towardEye=normalized(eye-position);
        const double incidence=std::abs(dot(normal,towardEye));
        const double fresnel=0.02+0.98*std::pow(1-incidence,5);
        const Vec3 reflected=normal*(2*dot(normal,sun))-sun;
        const double shine=explorer.underwater() ? 0 : std::pow(std::max(0.0,dot(reflected,towardEye)),72)*daylight;
        const double mud=ground.available ? 1-std::exp(-ground.sedimentKgM3/12) : 0;
        const double light=0.20+0.80*daylight;
        const Color clear{0.025f,0.29f,0.37f},silt{0.39f,0.29f,0.12f},sky{0.40f,0.61f,0.73f};
        Color color=wire ? Color{0.035f,0.11f,0.18f} : Color{
            static_cast<float>((mix(mix(clear.r,silt.r,mud),sky.r,fresnel)+shine*0.75)*light),
            static_cast<float>((mix(mix(clear.g,silt.g,mud),sky.g,fresnel)+shine*0.72)*light),
            static_cast<float>((mix(mix(clear.b,silt.b,mud),sky.b,fresnel)+shine*0.60)*light)};
        const double alpha=(0.20+0.75*(1-std::exp(-std::max(0.0,depth)*0.2))+0.45*fresnel)*smoothstep(0,0.035,depth);
        vertices[i]={local(position-origin),color,static_cast<float>(clamp(alpha,0.0,0.98)),depth};
    }
    for(const auto& t:waterTriangles_) {
        std::array<Vertex,4> clipped{}; std::size_t count=0;
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
            const double depth=sea-world.elevation(p); const auto flow=motion.current(p); const double speed=length(flow);
            if(depth<0.1 || speed<0.002) continue;
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
void Renderer::drawHud(const World& world,const Explorer& explorer,const Simulation& simulation,const ViewState& view) {
    const auto& weather=simulation.weather; const auto& water=simulation.water;
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
        rect(22,120,292,111,panel,0.88f);
        text(40,143,"LOCAL WEATHER / "+fixed(weather.layerAltitude()/1000,1)+" km ASL",mint);
        text(40,165,"WIND "+fixed(length(air.velocity))+" m/s  E "+fixed(air.wind.east)+" N "+fixed(air.wind.north),white);
        text(40,187,weather.cloudsEnabled() ? "RH "+fixed(air.humidity*100,0)+"%  CLOUD "+fixed(air.cloudWater,2)+" kg/m2" : "MOISTURE / CLOUDS DISABLED",white);
        text(40,215,simulation.motion.windExperiment() ? "WATER WIND "+fixed(*simulation.motion.windExperiment(),0)+" m/s [EXPERIMENT]" : "V wind tracers: "+std::string(view.windTracers ? "ON" : "OFF"),muted);
    }
    const auto liquid=liquidAt(world,&water,&simulation.motion,explorer.up());
    if(view.floraInfo && !view.globe) {
        rect(22,238,292,120,panel,.90f);
        text(40,259,"FLORA / nearest; "+std::to_string(plantSpeciesCount_)+" kinds",mint);
        if(focusPlant_) {
            const auto& p=*focusPlant_;
            text(40,280,std::string(plantTraits(p.species).name)+" / "+std::string(stageName(p.plant.stage)),white);
            text(40,301,"AGE "+fixed(p.plant.ageDays/365.25,2)+" y  HEIGHT "+fixed(p.height,2)+" m",white);
        } else text(40,284,"No visible plants nearby",muted);
        text(40,323,"BIO +"+fixed(simulation.flora.days(),1)+" d  "+(simulation.flora.rate()==1 ? "1x" : fixed(simulation.flora.rate()/86400,0)+"d/s [EXP]"),white);
        text(40,346,"Y speed / I growth / E water",muted);
    } else if(liquid.ocean) {
        rect(22,238,292,120,panel,0.90f);
        text(40,261,"OCEAN / WIND AND WAVES",mint);
        text(40,283,"HS "+fixed(simulation.motion.significantHeight(),2)+" m   FLOW "+fixed(length(liquid.current),3)+" m/s",white);
        text(40,305,"WIND10 "+fixed(length(simulation.motion.forcingWind()),1)+" m/s "+(simulation.motion.windExperiment() ? "[EXPERIMENT]" : "[ESTIMATE]"),white);
        text(40,327,"N wind: natural / 8 / 16 m/s",white);
        text(40,351,"K motion | U current tracers",muted);
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
    text(w-272,123,"SIMULATION MODULES",mint);
    for(unsigned i=0;i<moduleCount;++i) {
        const auto m=static_cast<Module>(i);
        const bool on=world.modules().enabled(m);
        const float y=145+16*static_cast<float>(i);
        const std::string key=i<10 ? std::to_string((i+1)%10) : (i==10 ? "M" : (i==11 ? "C" : (i==12 ? "K" : "I")));
        text(w-272,y,"["+key+"] "+std::string(moduleNames[i]),on?white:muted);
        text(w-90,y,on ? "ON" : (world.modules().requested(m) ? "WAIT" : "OFF"),on?mint:muted);
    }
    text(w-272,367,"BASE SPHERE  /  ALWAYS ACTIVE",muted);
    rect(22,h-51,w-44,29,panel,0.9f);
    text(35,h-31,(view.captured ? "ESC release mouse" : "CLICK to look") + std::string("   WASD move  F fly  G grid  O coast  Z seedlings  Y grow  H help"),white);
    if(view.help) {
        rect(w-358,h-232,336,165,panel,0.9f);
        text(w-340,h-208,explorer.flying ? "FLIGHT / "+fixed(explorer.flightSpeed,0)+" m/s" : (explorer.swimming() ? "SWIM / 1.6 m/s + CURRENT" : "WALK / 2.5 m/s"),mint);
        text(w-340,h-185,"SHIFT faster   SPACE jump / up",white);
        text(w-340,h-164,"CTRL down      WHEEL flight speed",white);
        text(w-340,h-143,"+ / - time     P pause",white);
        text(w-340,h-122,"HOME landing  1-9/0 M/C/K/I",white);
        text(w-340,h-101,"G surface: "+std::string(surfaceStyleName(view.surfaceStyle)),mint);
        text(w-340,h-80,"X grove  O coast  Y grow  E panel",muted);
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

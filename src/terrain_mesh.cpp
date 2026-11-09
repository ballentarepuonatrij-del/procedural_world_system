#include "planet/terrain_mesh.hpp"
#include "planet/rivers.hpp"
#include <stdexcept>

namespace planet {
namespace {
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
}
double terrainSpacing(const World& world,Vec3 observer,double altitude,double minimumStep) {
    if(!std::isfinite(length(observer)) || length(observer)<.5 || !std::isfinite(altitude) ||
       !std::isfinite(minimumStep) || minimumStep<2 || minimumStep>8192)
        throw std::invalid_argument("Invalid terrain mesh location or spacing");
    const double aboveGround=std::max(0.0,altitude-world.surface(observer));
    const int level=clamp(static_cast<int>(std::floor(std::log2(std::max(1.0,aboveGround/64)))),0,12);
    return std::max(minimumStep,std::ldexp(2.0,level));
}
TerrainMesh makeTerrainMesh(const World& world,const Hydrology& water,Vec3 observer,double altitude,double seconds,double minimumStep) {
    if(!std::isfinite(seconds)) throw std::invalid_argument("Invalid terrain mesh clock");
    TerrainMesh mesh;

    // Local +Z points south so the rendering basis is right-handed.
    mesh.anchor=observer; mesh.east=east(mesh.anchor); mesh.north=-north(mesh.anchor);
    const double radius=world.config().radius;
    mesh.spacing=terrainSpacing(world,observer,altitude,minimumStep);
    mesh.eyeHeight=altitude;
    Vec3 face;
    if(std::abs(mesh.anchor.x)>=std::abs(mesh.anchor.y) && std::abs(mesh.anchor.x)>=std::abs(mesh.anchor.z))
        face={std::copysign(1.0,mesh.anchor.x),0,0};
    else if(std::abs(mesh.anchor.y)>=std::abs(mesh.anchor.z)) face={0,std::copysign(1.0,mesh.anchor.y),0};
    else face={0,0,std::copysign(1.0,mesh.anchor.z)};
    const Vec3 reference=std::abs(face.y)<0.5 ? Vec3{0,1,0} : Vec3{0,0,1};
    const Vec3 right=normalized(cross(reference,face)), down=cross(right,face);
    const auto cave=world.caveSite(mesh.anchor);
    const double projection=radius/dot(mesh.anchor,face);
    auto xs=terrainAxis(dot(mesh.anchor,right)*projection,mesh.spacing);
    auto ys=terrainAxis(dot(mesh.anchor,down)*projection,mesh.spacing);
    if(water.enabled() && mesh.spacing<=8 && altitude-water.ground(world,mesh.anchor)<1200) {
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
    if(cave && altitude-world.surface(mesh.anchor)<1200) {
        double minX=1e30,maxX=-1e30,minY=1e30,maxY=-1e30;
        for(double z:{-85.,290.}) for(double x:{-110.,110.}) {
            const auto q=cave->directionAt(x,z); const double k=radius/dot(q,face);
            minX=std::min(minX,dot(q,right)*k); maxX=std::max(maxX,dot(q,right)*k);
            minY=std::min(minY,dot(q,down)*k); maxY=std::max(maxY,dot(q,down)*k);
        }
        const double step=std::max(mesh.spacing,std::pow(2.0,std::ceil(std::log2(2/std::pow(dot(cave->anchor,face),2)))));
        const auto refine=[&](std::vector<double>& axis,double low,double high) {
            for(double v=std::ceil(low/step)*step;v<=high;v+=step) axis.push_back(v);
            std::sort(axis.begin(),axis.end()); axis.erase(std::unique(axis.begin(),axis.end()),axis.end());
        };
        refine(xs,minX,maxX); refine(ys,minY,maxY);
    }
    mesh.rivers.clear();
    if(world.modules().enabled(Module::Rivers)) {
        const auto network=world.waterNetwork();mesh.rivers=network->nearby(mesh.anchor,2200);
        const auto river=network->sample(mesh.anchor);
        if(mesh.spacing<=8 && river.channel && river.distance<river.width*.5+40 && altitude-world.surface(mesh.anchor)<250) {
            const double extent=clamp(river.width*3,40.,192.),spacing=river.width<4 ? .5 : river.width<16 ? 2. : 8.;
            const auto refine=[&](std::vector<double>& axis,double center) {
                for(double p=std::ceil((center-extent)/spacing)*spacing;p<=center+extent;p+=spacing) axis.push_back(p);
                std::sort(axis.begin(),axis.end());axis.erase(std::unique(axis.begin(),axis.end()),axis.end());
            };
            refine(xs,dot(mesh.anchor,right)*projection);refine(ys,dot(mesh.anchor,down)*projection);
        }
    }
    const int columns=static_cast<int>(xs.size()),rows=static_cast<int>(ys.size());
    const double aboveGround=std::max(0.0,altitude-world.surface(mesh.anchor));
    struct Vertex { Vec3 position; Color color; double u,v; float gridAlpha; Vec3 normal{}; bool ocean{}; Vec3 unit{}; double height{}; CaveColumn cavity{}; Color rockColor{}; double snow=0; };
    std::vector<Vertex> vertices(xs.size()*ys.size());
    mesh.waterVertices.clear(); mesh.waterTriangles.clear();
    mesh.waterVertices.reserve(vertices.size());
    const Noise material(world.config().seed^0x695c17ULL);
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        const Vec3 p=normalized(face*radius+right*xs[static_cast<std::size_t>(x)]+down*ys[static_cast<std::size_t>(y)]);
        const auto sample=world.sample(p,seconds);
        const auto ground=water.sample(p);
        const auto river=world.modules().enabled(Module::Rivers) ? world.waterNetwork()->sample(p) : RiverSample{};
        const double original=sample.elevation;
        const double h=ground.available ? river.channel ? original+ground.bedChange*ground.weight : mix(original,ground.bed,ground.weight) : original;
        const double snow=cave && cave->protectedGround(p) ? 0 : water.snow.depth(p,sample);
        const Vec3 relative=p*(radius+h+snow)-mesh.anchor*radius;
        auto color=world.color(sample);
        if(sample.ocean) {
            const float shallow=static_cast<float>(1-clamp((world.config().seaLevel-sample.elevation)/150,0.0,1.0));
            color={0.13f+0.30f*shallow,0.19f+0.20f*shallow,0.20f+0.10f*shallow};
        } else {
            const float variation=static_cast<float>(0.87+0.22*material.fbm(p*(radius/45),2));
            const float moisture=static_cast<float>(ground.available ? 1-0.28*ground.saturation*ground.weight : 1);
            color.r*=variation*moisture; color.g*=variation*moisture; color.b*=variation*moisture;
        }
        if(river.wet) color={.34f,.30f,.22f};
        // World-anchored coordinates keep the material stationary when the local mesh recentres.
        const Vec3 global=p*(radius+h);
        const Vec3 position{dot(relative,mesh.east),dot(relative,mesh.anchor),dot(relative,mesh.north)};
        const double localSpacing=std::max(x>0 ? xs[static_cast<std::size_t>(x)]-xs[static_cast<std::size_t>(x-1)] : mesh.spacing,
                                           y>0 ? ys[static_cast<std::size_t>(y)]-ys[static_cast<std::size_t>(y-1)] : mesh.spacing);
        double waterBed=h;
        if(cave && cave->coastal) {const auto c=cave->column(p);if(c.valid()) waterBed=std::min(waterBed,c.floor);}
        mesh.waterVertices.push_back({p,position,waterBed,world.modules().enabled(Module::Ocean) || world.modules().enabled(Module::InlandWaters) || (ground.available && ground.weight>0),river.wet && river.level>world.config().seaLevel+.02,localSpacing,sample.inland,sample.landform==Landform::Wetland,sample.waterLevel});
        const float alpha=static_cast<float>(1-smoothstep(1500+aboveGround*4,10000+aboveGround*12,std::hypot(position.x,position.z)));
        const double band=0.5+0.5*std::sin(h/7.5+0.35*std::sin(h/27));
        Color rock{0.44f,0.42f,0.36f};
        if(sample.landform==Landform::Canyon || sample.landform==Landform::Plateau)
            rock={static_cast<float>(0.52+0.14*band),static_cast<float>(0.30+0.12*band),static_cast<float>(0.18+0.10*band)};
        else if(sample.landform==Landform::Karst) rock={static_cast<float>(0.48+0.12*band),static_cast<float>(0.46+0.12*band),static_cast<float>(0.39+0.12*band)};
        else if(sample.landform==Landform::Mountains || sample.landform==Landform::Gorge) rock={0.39f,0.41f,0.42f};
        if(cave && cave->protectedGround(p)) color=rock;
        vertices[static_cast<std::size_t>(y*columns+x)]={position,color,
            (global.x+global.z)*0.25,(global.y+global.z*0.37)*0.25,alpha,{},sample.ocean,p,h+snow,cave ? cave->column(p) : CaveColumn{},rock,snow};
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
            const double cover=smoothstep(0.,.06,v.snow)*smoothstep(.25,.8,v.normal.y);
            v.color={static_cast<float>(mix(v.color.r,.87,cover)),static_cast<float>(mix(v.color.g,.92,cover)),static_cast<float>(mix(v.color.b,.96,cover))};
        }
        // Grazing lines cover subpixel cells near the horizon. Fade them instead
        // of accumulating a bright, flickering band of overlapping edges.
        const double incidence=dot(v.normal,normalized(Vec3{0,mesh.eyeHeight,0}-v.position));
        v.gridAlpha*=static_cast<float>(smoothstep(0.0,0.08,incidence));
    }
    for(int y=0;y<rows-1;++y) for(int x=0;x<columns-1;++x) {
        const auto a=static_cast<std::size_t>(y*columns+x),b=a+1,c=a+static_cast<std::size_t>(columns),d=c+1;
        for(const auto tri:{std::array<std::size_t,3>{a,c,d},std::array<std::size_t,3>{a,d,b}})
            if(mesh.waterVertices[tri[0]].available && mesh.waterVertices[tri[1]].available && mesh.waterVertices[tri[2]].available) mesh.waterTriangles.push_back(tri);
    }
    mesh.waterRevision=water.revision(); mesh.waterSteps=water.steps();
    const auto interpolate=[&](const Vertex& a,const Vertex& b,double t) {
        Vertex v=a; v.unit=normalized(a.unit*(1-t)+b.unit*t); v.height=world.elevation(v.unit);
        const Vec3 relative=v.unit*(radius+v.height)-mesh.anchor*radius;
        v.position={dot(relative,mesh.east),dot(relative,mesh.anchor),dot(relative,mesh.north)};
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
    const auto terrainVertex=[&](const Vertex& v) {
        mesh.terrain.push_back({v.position,v.normal,v.color,v.u,v.v});
    };

    for(int y=0;y<rows-1;++y) for(int x=0;x<columns-1;++x) {
        for(const auto& tri:{std::array<Vertex,3>{at(x,y),at(x,y+1),at(x+1,y+1)},std::array<Vertex,3>{at(x,y),at(x+1,y+1),at(x+1,y)}}) {
            const auto polygon=cave ? clip({tri.begin(),tri.end()},1) : std::vector<Vertex>{tri.begin(),tri.end()};
            for(std::size_t i=1;i+1<polygon.size();++i) {
                const auto a=polygon[0].position,b=polygon[i].position,c=polygon[i+1].position;
                if(std::max({std::hypot(a.x,a.z),std::hypot(b.x,b.z),std::hypot(c.x,c.z)})<1200) mesh.casters.push_back({a,b,c});
                terrainVertex(polygon[0]); terrainVertex(polygon[i]); terrainVertex(polygon[i+1]);
            }
        }
    }



    if(cave) for(int y=0;y<rows-1;++y) for(int x=0;x<columns-1;++x) {
        for(const auto& tri:{std::array<Vertex,3>{at(x,y),at(x,y+1),at(x+1,y+1)},std::array<Vertex,3>{at(x,y),at(x+1,y+1),at(x+1,y)}}) {
            if(std::max({tri[0].cavity.field,tri[1].cavity.field,tri[2].cavity.field})<0) continue;
            const auto footprint=clip({tri.begin(),tri.end()},0);
            for(int side=2;side<=3;++side) {
                const auto polygon=clip(footprint,side);
                const auto point=[&](const Vertex& v) {
                    const double h=side==2 ? v.cavity.floor : v.cavity.ceiling;
                    const Vec3 p=v.unit*(radius+h)-mesh.anchor*radius;
                    return Vec3{dot(p,mesh.east),dot(p,mesh.anchor),dot(p,mesh.north)};
                };
                for(std::size_t i=1;i+1<polygon.size();++i) {
                    const auto a=point(polygon[0]),b=point(polygon[i]),c=point(polygon[i+1]);
                    auto normal=normalized(cross(b-a,c-a));
                    if((normal.y>0)!=(side==2)) normal=-normal;

                    for(const auto index:{std::size_t{0},i,i+1}) {
                        const auto& v=polygon[index]; const auto p=point(v);
                        const double height=side==2 ? v.cavity.floor : v.cavity.ceiling;
                        const float shade=static_cast<float>(0.83+0.12*std::sin(height*1.5)+0.05*std::sin(height*5));
                        mesh.caves.push_back({p,normal,{0.51f*shade,0.47f*shade,0.38f*shade},v.u,v.v});
                    }

                }
            }
        }
    }



    const auto edge=[&](int ax,int ay,int bx,int by,bool diagonal) {
        const auto& a=vertices[static_cast<std::size_t>(ay*columns+ax)];
        const auto& b=vertices[static_cast<std::size_t>(by*columns+bx)];
        if(a.gridAlpha==0 && b.gridAlpha==0) return;
        if(cave && (field(a,1)<0 || field(b,1)<0)) return;
        const auto vertex=[&](const Vertex& v) {
            mesh.grid.push_back({v.position,v.gridAlpha*(diagonal ? 0.34f : 0.75f)});
        };
        vertex(a); vertex(b);
    };
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        if(x+1<columns) edge(x,y,x+1,y,false);
        if(y+1<rows) edge(x,y,x,y+1,false);
        // Match the diagonal of the filled triangle strip, including its depth.
        if(x+1<columns && y+1<rows) edge(x,y,x+1,y+1,true);
    }

    mesh.plants.clear();
    if(aboveGround<1800 && minimumStep<=8) {
        const auto objects=surfaceSites(world,mesh.anchor,420,aboveGround<100);
        for(const auto& object:objects) {
            const double distance=length(object.up-mesh.anchor)*radius;
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
            Vec3 base=tx>=ty ? a*(1-tx)+at(ix+1,iy).position*(tx-ty)+d*ty
                                  : a*(1-ty)+at(ix,iy+1).position*(ty-tx)+d*tx;
            if(object.species==Species::WaterLily) {
                const auto relative=object.up*(radius+object.elevation)-mesh.anchor*radius;
                base={dot(relative,mesh.east),dot(relative,mesh.anchor),dot(relative,mesh.north)};
            }
            double snow=0;
            if(water.snow.enabled() && !plantTraits(object.species).aquatic) {
                const auto pack=water.snow.sample(object.up);
                snow=pack.available && pack.weight==1 ? pack.depth : water.snow.depth(object.up,world.sample(object.up,seconds));
            }
            mesh.plants.push_back({object,base,snow});
        }
    }
    mesh.seconds=seconds;mesh.snowRevision=water.snow.revision();return mesh;
}
} // namespace planet

#include "planet/grass.hpp"
#include <stdexcept>

namespace planet {
double grassCoverage(const GrassGround& g,double sea) {
    const auto& s=g.sample;
    if(!g.supported || s.ocean || s.waterDepth>.025 || !std::isfinite(s.temperature) || s.elevation<sea+.5) return 0;
    const double climate=smoothstep(260,281,s.temperature)*(1-smoothstep(309,321,s.temperature))*smoothstep(170,550,s.precipitation);
    const double shade=s.biome==Biome::Rainforest ? .16 : forestBiome(s.biome) ? .45 : 1;
    return climate*shade*(1-smoothstep(.28,.7,g.slope))*(1-.9*clamp(s.sand,0.,1.));
}
std::vector<GrassRoot> makeGrassRoots(const World& world,Vec3 observer,const std::function<GrassGround(Vec3)>& sample) {
    std::vector<GrassRoot> roots;
    if(!world.modules().enabled(Module::SurfaceDetail) || !world.modules().enabled(Module::Biomes)) return roots;
    observer=normalized(observer);const auto& config=world.config();const double radius=config.radius,cell=.8;
    const Noise patches(config.seed^0x4752415353ULL);
    constexpr std::array<Vec3,6> faces{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    for(std::size_t f=0;f<faces.size();++f) {
        const auto face=faces[f];if(dot(observer,face)<.5) continue;
        const auto right=normalized(cross(std::abs(face.y)<.5 ? Vec3{0,1,0} : Vec3{0,0,1},face)),down=cross(right,face);
        const double k=radius/dot(observer,face),cx=dot(observer,right)*k,cy=dot(observer,down)*k,bound=grassRadius*3.1+cell;
        const auto x0=static_cast<std::int64_t>(std::floor(std::max(-radius,cx-bound)/cell)),x1=static_cast<std::int64_t>(std::floor(std::min(radius,cx+bound)/cell));
        const auto y0=static_cast<std::int64_t>(std::floor(std::max(-radius,cy-bound)/cell)),y1=static_cast<std::int64_t>(std::floor(std::min(radius,cy+bound)/cell));
        for(auto y=y0;y<=y1;++y) for(auto x=x0;x<=x1;++x) {
            auto id=hash64(config.seed^hash64(f+753));id=hash64(id^static_cast<std::uint64_t>(x));id=hash64(id^static_cast<std::uint64_t>(y));
            const double px=(static_cast<double>(x)+.15+.7*randomUnit(id))*cell,py=(static_cast<double>(y)+.15+.7*randomUnit(id+1))*cell;
            if(px < -radius || px>=radius || py < -radius || py>=radius) continue;
            const auto up=normalized(face*radius+right*px+down*py);
            if(length(up-observer)*radius>grassRadius || randomUnit(id+2)>std::pow(dot(up,face),3)) continue;
            const auto g=sample(up);const double cover=grassCoverage(g,config.seaLevel);
            if(cover<=0) continue;
            const double patch=patches.value(up*(radius/13));
            if(randomUnit(id+3)>cover*(.62+.35*patch)) continue;
            const bool tall=randomUnit(id+4)<smoothstep(-.4,.5,patch)*(.25+.6*clamp(g.sample.precipitation/1200,0.,1.));
            const double h=tall ? .65+.7*randomUnit(id+5) : .12+.22*randomUnit(id+5);
            if(g.snow>=h-.035) continue;
            const float green=static_cast<float>(.82+.3*randomUnit(id+6));
            const double dry=1-smoothstep(250,900,g.sample.precipitation);
            const Color color{static_cast<float>(mix(.27,.47,dry))*green,static_cast<float>(mix(.48,.49,dry))*green,static_cast<float>(mix(.12,.21,dry))*green};
            roots.push_back({id,up,g.base,g.sample.elevation,g.snow,h,tall ? .024 : .035,clamp(cover,.25,1.),
                std::remainder(dot(up*radius,Vec3{.071,.033,.093})+.4*randomUnit(id+7),tau),color,tall,g.normal});
        }
    }
    std::sort(roots.begin(),roots.end(),[&](const auto& a,const auto& b){return dot(a.up,observer)>dot(b.up,observer);});
    if(roots.size()>maximumGrassRoots) roots.resize(maximumGrassRoots);
    return roots;
}
GrassMesh makeGrassMesh(std::span<const GrassRoot> roots,Vec3 observer,double radius,const Flora& flora) {
    GrassMesh mesh;mesh.vertices.reserve(std::min(maximumGrassVertices,roots.size()*36));
    for(const auto& root:roots) {
        const auto growth=plantState(Species::MeadowGrass,root.id,flora.days(),root.suitability,flora.young());
        const double height=root.height*growth.size-root.snow;
        if(height<.035 || growth.foliage<.05) continue;
        const double range=length(root.up-observer)*radius;
        const int blades=range<14 ? 12 : range<28 ? 6 : 3,segments=range<14 ? 2 : 1;
        const auto count=static_cast<std::size_t>(blades*(segments*2-1)*3);
        if(mesh.vertices.size()+count>maximumGrassVertices) break;
        const auto first=mesh.vertices.size();
        for(int blade=0;blade<blades;++blade) {
            const auto id=root.id+static_cast<unsigned>(blade)*31U;
            const double angle=tau*randomUnit(id+10),h=height*(.65+.35*randomUnit(id+11));
            const Vec3 side{std::cos(angle),0,std::sin(angle)},front{-side.z,0,side.x};
            auto offset=side*(.32*randomUnit(id+12));
            offset.y=-(root.normal.x*offset.x+root.normal.z*offset.z)/std::max(.1,root.normal.y);
            const auto base=root.base+offset;
            const auto point=[&](double t,double sign) {return base+Vec3{0,h*t,0}+front*(h*.2*t*t)+side*(root.width*(1-t)*sign);};
            const auto vertex=[&](Vec3 p,Vec3 normal,double t) {
                const float light=static_cast<float>((.52+.48*t)*growth.foliage);
                mesh.vertices.push_back({p,normal,base,{root.color.r*light,root.color.g*light,root.color.b*light},
                    static_cast<float>(t*t),static_cast<float>(h),static_cast<float>(root.phase),root.tall ? 1.f : 0.f});
            };
            const auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,double ta,double tb,double tc) {
                const auto normal=normalized(cross(b-a,c-a));vertex(a,normal,ta);vertex(b,normal,tb);vertex(c,normal,tc);
            };
            for(int j=0;j<segments;++j) {
                const double a=static_cast<double>(j)/segments,b=static_cast<double>(j+1)/segments;
                triangle(point(a,-1),point(a,1),point(b,-1),a,a,b);
                if(j+1<segments) triangle(point(a,1),point(b,1),point(b,-1),a,b,b);
            }
        }
        mesh.clumps.push_back({first,mesh.vertices.size()-first,root.up,root.base,height,root.tall});
        if(root.tall) ++mesh.tallCount;else ++mesh.shortCount;
    }
    return mesh;
}
void advanceGrassSpring(GrassSpring& s,Vec3 target,double seconds,double frequency) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>120 || !std::isfinite(frequency) || frequency<=0 || !std::isfinite(length(target)))
        throw std::invalid_argument("Invalid grass spring forcing");
    if(seconds==0) return;
    // Exact solution of an underdamped unit-mass spring for a constant target.
    const double omega=tau*frequency,damping=.7*omega,w=omega*std::sqrt(1-.7*.7),e=std::exp(-damping*seconds);
    const double c=std::cos(w*seconds),n=std::sin(w*seconds);
    const auto a=s.bend-target,b=(s.velocity+a*damping)/w;
    s.bend=target+(a*c+b*n)*e;s.velocity=((b*w-a*damping)*c-(a*w+b*damping)*n)*e;
}
Vec3 grassWindTarget(Vec3 wind,bool tall) {
    wind.y=0;const double speed=std::min(length(wind),40.);
    return normalized(wind)*std::min(tall ? .65 : .30,speed*speed*(tall ? .007 : .0025));
}
Vec3 bendGrassPosition(const GrassVertex& v,Vec3 bend,double visibility) {
    bend.y=0;const double amount=length(bend);if(amount>.97) bend=bend*(.97/amount);
    const double drop=(1-std::sqrt(std::max(0.,1-dot(bend,bend))))*v.height;
    const auto p=v.position+Vec3{bend.x*v.height,-drop,bend.z*v.height}*v.flex;
    return v.root+(p-v.root)*clamp(visibility,0.,1.);
}
void GrassField::clear() {initialized_=false;cells_={};active_=0;++revision_;}
Vec3 GrassField::local(Vec3 p) const {p=p-anchor_*radius_;return {dot(p,east_),dot(p,anchor_),dot(p,north_)};}
void GrassField::synchronize(const Config& c,Vec3 observer,std::span<const GrassRoot> roots,bool refresh) {
    if(!initialized_ || seed_!=c.seed || radius_!=c.radius || length(observer-anchor_)*c.radius>256) {
        clear();anchor_=normalized(observer);east_=east(anchor_);north_=north(anchor_);radius_=c.radius;seed_=c.seed;
        originX_=originZ_=-resolution/2;initialized_=true;refresh=true;
    }
    const auto at=local(observer*radius_);const int x=static_cast<int>(std::floor(at.x/spacing))-resolution/2,z=static_cast<int>(std::floor(at.z/spacing))-resolution/2;
    if(x!=originX_ || z!=originZ_) {
        std::array<Cell,resolution*resolution> next{};
        for(int row=0;row<resolution;++row) for(int col=0;col<resolution;++col) {
            const int oldX=col+x-originX_,oldZ=row+z-originZ_;
            if(oldX>=0 && oldZ>=0 && oldX<resolution && oldZ<resolution) next[static_cast<std::size_t>(row*resolution+col)]=cells_[static_cast<std::size_t>(oldZ*resolution+oldX)];
        }
        cells_=std::move(next);originX_=x;originZ_=z;refresh=true;++revision_;
    }
    if(!refresh) return;
    for(auto& cell:cells_) {cell.ground=0;cell.weight=0;}
    for(const auto& root:roots) {
        const auto p=local(root.up*(radius_+root.elevation+root.snow));
        const double gx=p.x/spacing-originX_,gz=p.z/spacing-originZ_;
        const int ix=static_cast<int>(std::floor(gx)),iz=static_cast<int>(std::floor(gz));
        for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
            const int col=ix+dx,row=iz+dz;if(col<0 || row<0 || col>=resolution || row>=resolution) continue;
            const double w=(dx ? gx-ix : 1-gx+ix)*(dz ? gz-iz : 1-gz+iz);
            auto& cell=cells_[static_cast<std::size_t>(row*resolution+col)];cell.ground+=p.y*w;cell.weight+=w;
        }
    }
    for(auto& cell:cells_) if(cell.weight>0) cell.ground/=cell.weight;
}
void GrassField::advance(double seconds,std::span<const GrassCollider> colliders) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>120 || colliders.size()>32) throw std::invalid_argument("Invalid grass interaction step");
    if(!initialized_ || seconds==0) return;
    std::array<Vec3,resolution*resolution> targets{};
    for(const auto& body:colliders) {
        if(!std::isfinite(length(body.position)) || !std::isfinite(length(body.previous)) || !std::isfinite(body.radius) || body.radius<=0 || body.radius>5 || !std::isfinite(body.height) || body.height<=0 || body.height>10)
            throw std::invalid_argument("Invalid grass collider");
        auto a=local(body.previous),b=local(body.position);if(length(b-a)>12) a=b; // teleport is not a swept bulldozer
        const auto delta=Vec3{b.x-a.x,0,b.z-a.z};const double distance2=dot(delta,delta),reach=body.radius+.9*spacing;
        if(std::max(a.x,b.x)+reach<originX_*spacing || std::min(a.x,b.x)-reach>(originX_+resolution-1)*spacing ||
           std::max(a.z,b.z)+reach<originZ_*spacing || std::min(a.z,b.z)-reach>(originZ_+resolution-1)*spacing) continue;
        const int x0=clamp(static_cast<int>(std::floor((std::min(a.x,b.x)-reach)/spacing))-originX_,0,resolution-1);
        const int x1=clamp(static_cast<int>(std::ceil((std::max(a.x,b.x)+reach)/spacing))-originX_,0,resolution-1);
        const int z0=clamp(static_cast<int>(std::floor((std::min(a.z,b.z)-reach)/spacing))-originZ_,0,resolution-1);
        const int z1=clamp(static_cast<int>(std::ceil((std::max(a.z,b.z)+reach)/spacing))-originZ_,0,resolution-1);
        for(int row=z0;row<=z1;++row) for(int col=x0;col<=x1;++col) {
            const auto i=static_cast<std::size_t>(row*resolution+col);const auto& cell=cells_[i];if(cell.weight<=0) continue;
            const Vec3 p{(originX_+col)*spacing,0,(originZ_+row)*spacing};
            const double t=distance2>1e-10 ? clamp(dot(p-a,delta)/distance2,0.,1.) : 1;
            const auto center=a+(b-a)*t;
            if(center.y>cell.ground+1.4 || center.y+body.height<cell.ground-.1) continue;
            auto away=Vec3{p.x-center.x,0,p.z-center.z};const double distance=length(away);
            if(distance>=reach) continue;
            if(distance<1e-8) away=distance2>1e-8 ? Vec3{-delta.z,0,delta.x} : Vec3{1,0,0};
            const auto target=normalized(away)*(.94*(1-smoothstep(body.radius*.1,reach,distance)));
            if(length(target)>length(targets[i])) targets[i]=target;
        }
    }
    active_=0;bool changed=false;
    for(std::size_t i=0;i<cells_.size();++i) {
        auto& s=cells_[i].spring;
        if(length(s.bend)+length(s.velocity)+length(targets[i])<1e-5) {s={};continue;}
        const auto before=s.bend;advanceGrassSpring(s,targets[i],seconds,1.35);
        if(length(s.bend-before)>1./65535) changed=true;
        ++active_;
    }
    if(changed) ++revision_;
}
Vec3 GrassField::sample(Vec3 worldPosition) const {
    if(!initialized_) return {};
    const auto p=local(worldPosition);const double x=p.x/spacing-originX_,z=p.z/spacing-originZ_;
    if(x<0 || z<0 || x>=resolution-1 || z>=resolution-1) return {};
    const int ix=static_cast<int>(std::floor(x)),iz=static_cast<int>(std::floor(z));Vec3 result{};
    for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx)
        result+=cells_[static_cast<std::size_t>((iz+dz)*resolution+ix+dx)].spring.bend*((dx ? x-ix : 1-x+ix)*(dz ? z-iz : 1-z+iz));
    return result*smoothstep(0,2,std::min({x,z,resolution-1-x,resolution-1-z}));
}
std::array<unsigned short,GrassField::resolution*GrassField::resolution*2> GrassField::texture() const {
    std::array<unsigned short,resolution*resolution*2> data{};
    for(std::size_t i=0;i<cells_.size();++i) {
        data[2*i]=static_cast<unsigned short>(std::lround((clamp(cells_[i].spring.bend.x,-1.,1.)*.5+.5)*65535));
        data[2*i+1]=static_cast<unsigned short>(std::lround((clamp(cells_[i].spring.bend.z,-1.,1.)*.5+.5)*65535));
    }
    return data;
}
double GrassField::maximumBend() const {double value=0;for(const auto& c:cells_) value=std::max(value,length(c.spring.bend));return value;}
} // namespace planet

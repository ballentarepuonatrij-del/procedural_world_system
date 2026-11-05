#include "planet/landforms.hpp"
#include "planet/model.hpp"
#include "planet/rivers.hpp"
#include <limits>

namespace planet {
std::optional<Landform> landmarkType(std::string_view name) {
    if(name=="cave") return Landform::Karst;
    if(name=="canyon") return Landform::Canyon;
    if(name=="mountains") return Landform::Mountains;
    if(name=="gorge") return Landform::Gorge;
    return {};
}
std::string_view landformName(Landform type) {
    constexpr std::array<std::string_view,6> names{"Lowland","Mountains","Plateau","Canyon","Gorge","Karst caves"};
    return names.at(static_cast<std::size_t>(type));
}
Relief shapeLandforms(Vec3 p,double radius,double seaLevel,std::uint64_t seed,double height,double mountainWeight) {
    const Noise noise(seed^0x6e0109ULL); const Vec3 m=p*radius;
    const double dry=smoothstep(seaLevel+50,seaLevel+450,height);
    const double province=noise.value(m/90000+Vec3{27,14,93});
    const double plateau=smoothstep(-0.05,0.25,province)*dry;
    const double alpine=clamp(mountainWeight+smoothstep(-0.25,0.15,-province)*0.65,0.0,1.0)*dry;
    const double bend=noise.fbm(m/14000+Vec3{83,21,4},3);
    const double folds=std::sin((m.x*0.63+m.y*0.41+m.z*0.66)/2600+3*bend);
    const double ribs=std::sin((m.x*0.79-m.y*0.52+m.z*0.25)/950+2*bend);
    const double ridges=std::pow(1-std::abs(folds),2)*(0.65+0.35*std::pow(1-std::abs(ribs),2));
    const double raised=alpine*(1100*ridges+130*noise.fbm(m/1700,3));
    const double channel=std::abs(std::sin((m.x*0.32-m.y*0.76+m.z*0.57)/8500+2.2*noise.fbm(m/7000,3)));
    const double canyon=plateau*(1-smoothstep(0.018,0.10,channel));
    const double inner=plateau*(1-smoothstep(0.004,0.020,channel));
    const double gorge=alpine*(1-smoothstep(0.006,0.045,channel));
    // Smooth benches approximate alternating resistant/weak sedimentary beds.
    // No hard height quantization: slopes and face-chart boundaries stay continuous.
    const double bench=30*plateau*std::sin((height+430*plateau)/48)*smoothstep(0.025,0.14,channel);
    const double incision=330*canyon+150*inner+220*gorge;
    Relief result{height+430*plateau+raised+bench-incision,incision,alpine,Landform::Lowland};
    if(alpine>0.25) result.type=Landform::Mountains;
    if(plateau>0.45) result.type=Landform::Plateau;
    if(canyon>0.25) result.type=Landform::Canyon;
    if(gorge>0.25 && canyon<0.25) result.type=Landform::Gorge;
    return result;
}
CaveAddress caveAddress(Vec3 p,double radius,std::uint64_t seed,int offsetX,int offsetY) {
    p=normalized(p); Vec3 face; std::uint64_t index=0;
    if(std::abs(p.x)>=std::abs(p.y) && std::abs(p.x)>=std::abs(p.z)) { face={std::copysign(1.0,p.x),0,0}; index=p.x>0 ? 0 : 1; }
    else if(std::abs(p.y)>=std::abs(p.z)) { face={0,std::copysign(1.0,p.y),0}; index=p.y>0 ? 2 : 3; }
    else { face={0,0,std::copysign(1.0,p.z)}; index=p.z>0 ? 4 : 5; }
    const Vec3 reference=std::abs(face.y)<0.5 ? Vec3{0,1,0} : Vec3{0,0,1};
    const Vec3 right=normalized(cross(reference,face)),down=cross(right,face);
    constexpr double cell=16000;
    const auto x=static_cast<std::int64_t>(std::floor(dot(p,right)*radius/(dot(p,face)*cell)))+offsetX;
    const auto y=static_cast<std::int64_t>(std::floor(dot(p,down)*radius/(dot(p,face)*cell)))+offsetY;
    auto id=hash64(seed^0xca4e512ULL^index); id=hash64(id^static_cast<std::uint64_t>(x)); id=hash64(id^static_cast<std::uint64_t>(y));
    const double px=(static_cast<double>(x)+0.25+0.5*randomUnit(id))*cell;
    const double py=(static_cast<double>(y)+0.25+0.5*randomUnit(id+1))*cell;
    const bool safe=std::abs(px)<radius-1800 && std::abs(py)<radius-1800 && randomUnit(id+2)<0.55;
    return {id,normalized(face*radius+right*px+down*py),safe};
}
CaveSite makeCave(CaveAddress address,double radius,double base) {
    CaveSite site; site.id=address.id; site.anchor=address.anchor; site.radius=radius; site.base=base;
    const double angle=tau*randomUnit(address.id+3);
    site.forward=north(site.anchor)*std::cos(angle)+east(site.anchor)*std::sin(angle);
    site.right=normalized(cross(site.forward,site.anchor));
    const double twist=6*(randomUnit(address.id+4)-0.5);
    site.nodes={{{0,-65,4,8,6},{0,-25,4,8,6},{10+twist,25,2,9,6},
                 {-14,70,0,12,7},{5+twist,125,-3,9,6},{20,190,-7,12,8},{0,250,-8,27,14},
                 {-40,110,-2,7,5},{-65,165,-5,10,7},{-52,205,-6,18,10}}};
    site.links={{{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{3,7},{7,8},{8,9}}};
    return site;
}
Vec3 CaveSite::directionAt(double x,double z) const { return normalized(anchor+(right*x+forward*z)/radius); }
Vec3 CaveSite::local(Vec3 unit) const {
    unit=normalized(unit); const double facing=dot(unit,anchor);
    if(facing<0.5) return {1e20,0,1e20};
    return {dot(unit,right)*radius/facing,0,dot(unit,forward)*radius/facing};
}
double CaveSite::cover(Vec3 unit,double original) const {
    const auto p=local(unit);
    const double mask=(1-smoothstep(95,190,std::abs(p.x)))*smoothstep(-140,-85,p.z)*(1-smoothstep(380,500,p.z));
    const double height=68*smoothstep(-40,65,p.z)*(1-smoothstep(310,410,p.z));
    return mix(original,base+height,mask);
}
CaveColumn CaveSite::column(Vec3 unit) const { const auto p=local(unit); return columnLocal(p.x,p.z); }
CaveColumn CaveSite::columnLocal(double x,double z) const {
    CaveColumn result{base,base,-1e20}; double nearestY=base;
    double low=std::numeric_limits<double>::infinity(),high=-low;
    for(const auto link:links) {
        const auto a=nodes[link[0]],b=nodes[link[1]];
        const double dx=b.x-a.x,dz=b.z-a.z;
        const double t=clamp(((x-a.x)*dx+(z-a.z)*dz)/(dx*dx+dz*dz),0.0,1.0);
        const double px=x-mix(a.x,b.x,t),pz=z-mix(a.z,b.z,t),width=mix(a.width,b.width,t);
        const double field=1-(px*px+pz*pz)/(width*width);
        const double center=base+mix(a.y,b.y,t);
        if(field>result.field) { result.field=field; nearestY=center; }
        if(field>=0) {
            const double h=mix(a.height,b.height,t)*std::sqrt(field);
            low=std::min(low,center-h); high=std::max(high,center+h);
        }
    }
    result.floor=std::isfinite(low) ? low : nearestY;
    result.ceiling=std::isfinite(high) ? high : nearestY;
    return result;
}
bool CaveSite::protectedGround(Vec3 unit) const { const auto p=local(unit); return std::abs(p.x)<110 && p.z>-85 && p.z<290; }
std::optional<CaveSite> World::caveSite(Vec3 p,double reach,bool requireEnabled) const {
    if(!modules_.enabled(Module::Landforms) || (requireEnabled && !modules_.enabled(Module::Caves))) return {};
    const auto address=caveAddress(p,config_.radius,config_.seed);
    if(!address.eligible || length(normalized(p)-address.anchor)*config_.radius>reach) return {};
    if(caveCacheId_!=address.id) {
        caveCacheId_=address.id; caveCache_.reset();
        const double height=rawRelief(address.anchor).elevation;
        const double slope=std::max(std::abs(rawRelief(normalized(address.anchor+east(address.anchor)*(100/config_.radius))).elevation-height),
                                    std::abs(rawRelief(normalized(address.anchor+north(address.anchor)*(100/config_.radius))).elevation-height))/100;
        if(height>config_.seaLevel+80 && height<4000 && slope<0.3) caveCache_=makeCave(address,config_.radius,height);
    }
    return caveCache_;
}
Relief World::relief(Vec3 p) const {
    auto result=rawRelief(p);
    if(modules_.enabled(Module::Rivers)) {
        const auto river=waterNetwork()->sample(p,result.elevation);
        if(river.channel) result.elevation=river.bed;
    }
    if(const auto site=caveSite(p,550,false)) {
        const double h=site->cover(p,result.elevation);
        if(std::abs(h-result.elevation)>0.1 || site->protectedGround(p)) result.type=Landform::Karst;
        result.elevation=h;
    }
    return result;
}
Landmark World::findCoast() const {
    const Vec3 home=findLand(); const double radius=config_.radius,sea=config_.seaLevel;
    double score=1e30; Landmark chosen{home,home,Landform::Lowland};
    if(!modules_.enabled(Module::Terrain) || !modules_.enabled(Module::Ocean)) return chosen;
    for(int ray=0;ray<48;++ray) {
        const double angle=tau*ray/48;
        const Vec3 axis=east(home)*std::cos(angle)+north(home)*std::sin(angle);
        Vec3 dry=home;
        for(int step=1;step<=120;++step) {
            Vec3 wet=normalized(home+axis*(step*5000/radius));
            if(elevation(wet)>sea) { dry=wet; continue; }
            for(int i=0;i<32;++i) { const auto mid=normalized(dry+wet); if(elevation(mid)>sea) dry=mid; else wet=mid; }
            const Vec3 shore=normalized(dry+wet),out=normalized(axis-shore*dot(axis,shore));
            const auto position=normalized(shore-out*(30/radius));
            Vec3 offshore=shore;
            for(double distance:{100.,200.,400.,800.,1600.}) {
                offshore=normalized(shore+out*(distance/radius)); if(elevation(offshore)<sea-12) break;
            }
            const double h=elevation(position),bottom=elevation(offshore);
            if(h>sea && h<sea+12 && bottom<sea-8) {
                const double merit=length(shore-home)*radius/10000+(h-sea)*4;
                if(merit<score) { score=merit; chosen={position,offshore,relief(position).type}; }
            }
            break;
        }
    }
    return chosen;
}
Landmark World::findLandmark(Landform type,Vec3 around) const {
    around=normalized(around);
    if(!modules_.enabled(Module::Landforms)) return {findLand(),around,Landform::Lowland};
    if(type==Landform::Karst && modules_.enabled(Module::Caves)) {
        double best=1e30; std::optional<CaveSite> chosen;
        for(int y=-5;y<=5;++y) for(int x=-5;x<=5;++x) {
            const auto address=caveAddress(around,config_.radius,config_.seed,x,y);
            if(!address.eligible) continue;
            const auto site=caveSite(address.anchor);
            if(site) {
                const double distance=length(site->anchor-around);
                if(distance<best) { best=distance; chosen=site; }
            }
        }
        if(chosen) return {chosen->directionAt(0,-95),chosen->directionAt(0,40),Landform::Karst};
    }
    double best=-1e30; Vec3 target=around;
    // High mountain belts can lie far inland from the lowland landing site.
    const double surveyStep=type==Landform::Mountains ? 30000 : 2500;
    for(int y=-40;y<=40;++y) for(int x=-40;x<=40;++x) {
        const auto p=normalized(around+(east(around)*(x*surveyStep)+north(around)*(y*surveyStep))/config_.radius);
        const auto r=relief(p);
        if(r.type!=type || r.elevation<config_.seaLevel+50) continue;
        double merit=r.incision*5-std::hypot(x,y)*12;
        if(type==Landform::Mountains) {
            double low=r.elevation;
            for(const auto axis:{east(p),-east(p),north(p),-north(p)})
                low=std::min(low,elevation(normalized(p+axis*(1200/config_.radius))));
            merit=r.elevation-low+0.12*r.elevation-std::hypot(x,y)*2;
        }
        if(merit>best) { best=merit; target=p; }
    }
    if(best==-1e30) return {findLand(),around,Landform::Lowland};
    // Resolve a local summit / channel bottom missed by the regional search.
    const Vec3 regional=target;
    double localBest=-1e30;
    for(int y=-10;y<=10;++y) for(int x=-10;x<=10;++x) {
        const auto p=normalized(regional+(east(regional)*(x*125)+north(regional)*(y*125))/config_.radius);
        const auto r=relief(p);
        if(r.type!=type || r.elevation<config_.seaLevel+50) continue;
        const double merit=type==Landform::Mountains ? r.elevation : r.incision*5-r.elevation*0.1;
        if(merit>localBest) { localBest=merit; target=p; }
    }
    // Pick an exposed dry viewpoint, penalizing terrain that hides the feature.
    double score=-1e30; Vec3 position=target;
    const double targetHeight=elevation(target);
    for(int ring=0;ring<4;++ring) for(int i=0;i<32;++i) {
        const double range=type==Landform::Mountains ? 1200+600*ring : 80+120*ring;
        const auto p=normalized(target+(east(target)*std::cos(tau*i/32)+north(target)*std::sin(tau*i/32))*(range/config_.radius));
        const double h=elevation(p);
        if(h<=config_.seaLevel+10) continue;
        double obstruction=0;
        for(int j=1;j<32;++j) {
            const double t=j/32.0;
            const auto q=normalized(p*(1-t)+target*t);
            obstruction=std::max(obstruction,elevation(q)-mix(h+1.75,targetHeight+8,t));
        }
        const double value=(type==Landform::Mountains ? -std::abs(h-targetHeight+650) : h-targetHeight)-12*obstruction;
        if(h>config_.seaLevel+10 && value>score) { score=value; position=p; }
    }
    return {position,target,type};
}
} // namespace planet

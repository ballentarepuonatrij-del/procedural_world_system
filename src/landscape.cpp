#include "planet/landscape.hpp"
#include "planet/rivers.hpp"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <type_traits>

namespace planet {
bool inlandLandform(Landform k) {return k==Landform::Wetland || k==Landform::Pond || k==Landform::Lake;}
bool coastalLandform(Landform k) {return k==Landform::Bay || k==Landform::Cliff || k==Landform::Spit || k==Landform::Grotto;}
namespace {
constexpr double tileSize=16000;
struct Address {std::uint64_t id;Vec3 face,right,down,center;double x,y;bool safe;};
Address address(Vec3 p,double radius,std::uint64_t seed,int ox=0,int oy=0) {
    p=normalized(p);Vec3 face;std::uint64_t index;
    if(std::abs(p.x)>=std::abs(p.y) && std::abs(p.x)>=std::abs(p.z)) {face={std::copysign(1.,p.x),0,0};index=p.x>0 ? 0 : 1;}
    else if(std::abs(p.y)>=std::abs(p.z)) {face={0,std::copysign(1.,p.y),0};index=p.y>0 ? 2 : 3;}
    else {face={0,0,std::copysign(1.,p.z)};index=p.z>0 ? 4 : 5;}
    const Vec3 right=normalized(cross(std::abs(face.y)<.5 ? Vec3{0,1,0} : Vec3{0,0,1},face)),down=cross(right,face);
    const auto x=static_cast<std::int64_t>(std::floor(dot(p,right)*radius/(dot(p,face)*tileSize)))+ox;
    const auto y=static_cast<std::int64_t>(std::floor(dot(p,down)*radius/(dot(p,face)*tileSize)))+oy;
    auto id=hash64(seed^0x1a4d5ca9ULL^index);id=hash64(id^static_cast<std::uint64_t>(x));id=hash64(id^static_cast<std::uint64_t>(y));
    const double cx=(static_cast<double>(x)+.5)*tileSize,cy=(static_cast<double>(y)+.5)*tileSize;
    return {id,face,right,down,normalized(face*radius+right*cx+down*cy),cx,cy,std::abs(cx)+tileSize*.5<radius && std::abs(cy)+tileSize*.5<radius};
}
double margin(const Address& a,Vec3 p,double radius) {
    const double f=dot(p,a.face);
    return (tileSize*.5-std::max(std::abs(dot(p,a.right)*radius/f-a.x),std::abs(dot(p,a.down)*radius/f-a.y)))*f*f;
}
}
Vec3 LandscapePatch::local(Vec3 p) const {const double f=dot(p,anchor);return f>.5 ? Vec3{dot(p,right)*radius/f,0,dot(p,forward)*radius/f} : Vec3{1e20,0,1e20};}
Vec3 LandscapePatch::directionAt(double x,double z) const {return normalized(anchor+(right*x+forward*z)/radius);}
double LandscapePatch::outline(Vec3 p) const {
    const auto q=local(p);const double angle=std::atan2(q.z/length,q.x/width);
    return std::hypot(q.x/width,q.z/length)/(1+.10*std::sin(3*angle+randomUnit(id)*tau)+.055*std::sin(5*angle+randomUnit(id+1)*tau));
}
bool LandscapePatch::affects(Vec3 p) const {
    const auto q=local(p);
    return inlandLandform(kind) ? outline(p)<1.5 : std::max(std::abs(q.x)/width,std::abs(q.z)/length)<1.5;
}
Relief LandscapePatch::apply(Vec3 p,Relief original) const {
    const auto q=local(p);const double distance=std::max(std::abs(q.x)/width,std::abs(q.z)/length);
    if(distance>(inlandLandform(kind) ? 1.75 : 1.55)) return original;
    double target=original.elevation,weight=0;
    const Noise noise(id^0x7e71a4dULL);
    if(inlandLandform(kind)) {
        const double r=outline(p);weight=1-smoothstep(1.25,1.50,r);if(weight<=0) return original;
        const double wet=1-smoothstep(.94,1.24,r);
        if(kind==Landform::Wetland) {
            const auto m=p*radius;
            const double hummock=.65*noise.fbm(m/85,2),pools=std::max(0.,noise.value(m/310)-.08)*3;
            target=level-.12+hummock-pools+3.5*smoothstep(.82,1.23,r);
        } else target=level+depth*(std::pow(r,4)-1);
        original.inland=r<1.25;original.waterLevel=level;original.wetness=wet;
    } else {
        weight=1-smoothstep(1.05,1.50,distance);if(weight<=0) return original;
        const double x=q.x/width;
        if(kind==Landform::Bay) {
            const double shore=.62*length*std::exp(-std::pow(x/.63,4));
            target=level+.045*(q.z-shore);original.sand=weight*(1-smoothstep(5,24,std::abs(target-level)));
        } else if(kind==Landform::Spit) {
            target=level+.035*q.z;
            const std::array<Vec3,5> path{{{-.95,0,.38},{-.62,0,-.06},{-.05,0,-.43},{.64,0,-.54},{.85,0,-.35}}};
            double d=1e30;
            for(std::size_t i=1;i<path.size();++i) {
                const Vec3 a{path[i-1].x*width,0,path[i-1].z*length},b{path[i].x*width,0,path[i].z*length};
                const Vec3 v=b-a;const double t=clamp(dot(q-a,v)/dot(v,v),0.,1.);
                d=std::min(d,planet::length(q-(a+v*t)));
            }
            const double breadth=clamp(width*.055,28.,100.);
            target=std::max(target,level+2.8-8*std::pow(d/breadth,2));
            original.sand=weight*(1-smoothstep(6,30,std::abs(target-level)));
        } else {
            const double bend=kind==Landform::Grotto ? 0 : 18*std::sin(q.x/110)+35*std::sin(q.x/430);
            const double face=q.z-bend;
            target=level-5+6*std::exp(-std::pow((face+80)/45,4))+depth*smoothstep(-38,32,face)+.012*std::max(0.,face);
            original.sand=weight*(1-smoothstep(1.5,5,std::abs(target-level)));
        }
    }
    original.elevation=mix(original.elevation,target,weight);
    if(weight>.05) original.type=kind;
    return original;
}
CaveSite LandscapePatch::cave() const {
    CaveSite c;c.id=id;c.anchor=anchor;c.forward=forward;c.right=right;c.radius=radius;c.base=level;c.coastal=true;
    c.nodes={{{0,-75,4,11,5},{0,-25,5,12,5},{3,22,7,13,6},{-9,65,8,16,7},{4,108,9,13,7},
              {14,155,10,18,8},{0,205,12,27,10},{-34,92,8,9,6},{-55,132,8,11,6},{-48,166,9,18,7}}};
    c.links={{{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{3,7},{7,8},{8,9}}};return c;
}
struct LandscapeAtlas::Impl {
    World base;std::shared_ptr<const RiverNetwork> rivers;std::uint64_t identity;
    mutable std::mutex mutex;
    mutable std::unordered_map<std::uint64_t,std::optional<LandscapePatch>> cache;
    explicit Impl(World w,std::shared_ptr<const RiverNetwork> r) : base(std::move(w)),rivers(std::move(r)) {
        static std::atomic<std::uint64_t> next{1};identity=next.fetch_add(1,std::memory_order_relaxed);
    }
    std::optional<LandscapePatch> build(const Address& a) const {
        if(!a.safe) return {};
        const auto& config=base.config();const double radius=config.radius,sea=config.seaLevel;
        const double h=base.naturalElevation(a.center);
        LandscapePatch s;s.id=a.id;s.anchor=a.center;s.radius=radius;
        // Coasts are located against the unmodified shore; never recursively
        // query conditioned terrain while generating a tile.
        if(base.modules().enabled(Module::Landforms) && std::abs(h-sea)<350) {
            std::optional<Vec3> shore;Vec3 inland;
            for(int i=0;i<8 && !shore;++i) {
                const auto axis=east(a.center)*std::cos(tau*i/8)+north(a.center)*std::sin(tau*i/8);
                const auto other=normalized(a.center+axis*(3500*dot(a.center,a.face)/radius));
                const double otherH=base.naturalElevation(other);
                if((h>sea)==(otherH>sea)) continue;
                Vec3 dry=h>sea ? a.center : other,wet=h>sea ? other : a.center;
                for(int j=0;j<18;++j) {const auto m=normalized(dry+wet);if(base.naturalElevation(m)>sea) dry=m;else wet=m;}
                shore=normalized(dry+wet);
                const double dx=base.naturalElevation(normalized(*shore+east(*shore)*(60/radius)))-base.naturalElevation(normalized(*shore-east(*shore)*(60/radius)));
                const double dz=base.naturalElevation(normalized(*shore+north(*shore)*(60/radius)))-base.naturalElevation(normalized(*shore-north(*shore)*(60/radius)));
                inland=normalized(east(*shore)*dx+north(*shore)*dz);
            }
            if(shore && planet::length(inland)>.5) {
                s.anchor=*shore;s.forward=inland;s.right=normalized(cross(inland,*shore));
                s.width=std::min(2000.,margin(a,*shore,radius)/2.2);s.length=s.width*.88;
                s.kind=std::array{Landform::Bay,Landform::Cliff,Landform::Spit,Landform::Grotto}[a.id%4];
                s.level=sea;s.depth=35+40*randomUnit(a.id+5);
                if(s.width>=650 && rivers->nearby(s.anchor,s.width*1.6+120).empty()) return s;
            }
        }
        if(h<sea+50 || h>sea+1800) return {};
        const double latitudeDegrees=std::abs(degrees(latitude(a.center)));
        const double rain=(120+2100*std::exp(-std::pow(latitudeDegrees/16,2))+1000*std::exp(-std::pow((latitudeDegrees-52)/17,2)))*(.75+.5*Noise(config.seed).value(a.center*18));
        if(rain<550) return {};
        const double energy=(.65*base.annualInsolation(latitude(a.center))+.35*config.solarConstant/4)*(1-config.albedo);
        const double annualTemperature=std::pow(energy/(stefanBoltzmann*config.emissivity),.25)-.0065*h;
        if(annualTemperature<278 || annualTemperature>306) return {};
        s.anchor=a.center;s.forward=north(s.anchor);s.right=east(s.anchor);
        s.kind=std::array{Landform::Wetland,Landform::Pond,Landform::Lake}[hash64(a.id+9)%3];
        s.width=(s.kind==Landform::Wetland ? 2200+900*randomUnit(a.id+3) : s.kind==Landform::Lake ? 650+1250*randomUnit(a.id+3) : 50+100*randomUnit(a.id+3));
        s.width=std::min(s.width,margin(a,s.anchor,radius)/1.8);s.length=s.width*(.60+.32*randomUnit(a.id+4));
        s.depth=s.kind==Landform::Wetland ? 1 : s.kind==Landform::Lake ? 6+20*randomUnit(a.id+5) : 1.2+2.4*randomUnit(a.id+5);
        s.level=h-(s.kind==Landform::Wetland ? .3 : s.depth*.3);
        double low=h,high=h;
        for(int i=0;i<8;++i) {const double angle=tau*i/8;const double v=base.naturalElevation(s.directionAt(std::cos(angle)*s.width*1.5,std::sin(angle)*s.length*1.5));low=std::min(low,v);high=std::max(high,v);}
        if(low<sea+20 || high-low>(s.kind==Landform::Wetland ? 95 : s.kind==Landform::Lake ? 160 : 35)) return {};
        if(!rivers->nearby(s.anchor,s.width*1.65+120).empty()) return {};
        const auto oldCave=caveAddress(s.anchor,radius,config.seed);
        if(oldCave.eligible && planet::length(oldCave.anchor-s.anchor)*radius<s.width*1.65+550) return {};
        return s;
    }
    std::optional<LandscapePatch> get(const Address& a) const {
        // Small value records need neither reference-count traffic nor TLS
        // destructor callbacks when a background mesh worker exits.
        struct Fast {std::uint64_t owner=0,id=0;std::optional<LandscapePatch> patch;};
        static_assert(std::is_trivially_destructible_v<Fast>);
        static thread_local std::array<Fast,64> fast;
        auto& slot=fast[a.id%fast.size()];if(slot.owner==identity && slot.id==a.id) return slot.patch;
        {const std::lock_guard lock(mutex);const auto it=cache.find(a.id);
            if(it!=cache.end()) {slot={identity,a.id,it->second};return it->second;}}
        auto value=build(a);
        {const std::lock_guard lock(mutex);if(cache.size()>=4096) cache.erase(cache.begin());value=cache.emplace(a.id,value).first->second;}
        slot={identity,a.id,value};return value;
    }
};
LandscapeAtlas::LandscapeAtlas(const World& world) {
    auto reference=world;reference.landscape_.reset();reference.waterNetwork_.reset();
    reference.modules().set(Module::Rivers,false);reference.modules().set(Module::InlandWaters,false);
    impl_=std::make_shared<Impl>(std::move(reference),world.waterNetwork());
}
std::optional<LandscapePatch> LandscapeAtlas::patch(Vec3 p) const {return impl_->get(address(p,impl_->base.config().radius,impl_->base.config().seed));}
Relief LandscapeAtlas::apply(Vec3 p,Relief r,bool inland,bool coast) const {
    const auto s=patch(p);return s && ((inland && inlandLandform(s->kind)) || (coast && coastalLandform(s->kind))) ? s->apply(p,r) : r;
}
Landmark LandscapeAtlas::find(Landform kind,Vec3 around) const {
    if(!inlandLandform(kind) && !coastalLandform(kind)) throw std::invalid_argument("Not a basin or coastal landmark");
    const auto& c=impl_->base.config();std::optional<LandscapePatch> chosen;double best=1e30;
    for(int ring=0;ring<=32 && !chosen;++ring) for(int y=-ring;y<=ring;++y) for(int x=-ring;x<=ring;++x) {
        if(std::max(std::abs(x),std::abs(y))!=ring) continue;
        const auto s=impl_->get(address(around,c.radius,c.seed,x,y));if(!s || s->kind!=kind) continue;
        const double d=planet::length(s->anchor-around);if(d<best) {chosen=s;best=d;}
    }
    // A dry landing region need not contain wetlands. Survey the globe rather
    // than forcing a water-rich habitat into an unsuitable local climate.
    if(!chosen) for(int i=0;i<8192;++i) {
        const auto p=direction(std::asin(1-2*(i+.5)/8192),i*pi*(3-std::sqrt(5.))+randomUnit(c.seed)*tau);
        const auto s=patch(p);if(!s || s->kind!=kind) continue;
        const double d=planet::length(s->anchor-around);if(d<best) {chosen=s;best=d;}
    }
    if(!chosen) throw std::runtime_error("No suitable basin or coastal site in the surveyed region");
    const auto& s=*chosen;Vec3 position,target;
    if(inlandLandform(kind)) {
        const double shore=s.length/s.outline(s.directionAt(0,-s.length));
        position=s.directionAt(0,-shore*1.025);target=s.directionAt(0,-shore*.78);
        if(kind==Landform::Wetland) {
            double score=1e30;
            for(int z=-8;z<=8;++z) for(int x=-8;x<=8;++x) {
                const auto p=s.directionAt(x*30,z*30);const auto h=s.apply(p,{}).elevation;
                const double merit=std::abs(h-s.level-.18)+std::hypot(x,z)*.002;
                if(merit<score) {score=merit;position=p;target=s.directionAt(x*30,z*30+60);}
            }
        }
    } else if(kind==Landform::Grotto) {position=s.directionAt(0,-85);target=s.directionAt(0,55);}
    else if(kind==Landform::Cliff) {position=s.directionAt(-s.width*.35,-85);target=s.directionAt(0,35);}
    else if(kind==Landform::Spit) {position=s.directionAt(-s.width*.32,-s.length*.255);target=s.directionAt(s.width*.65,-s.length*.53);}
    else {position=s.directionAt(0,s.length*.62+55);target=s.directionAt(0,-s.length*.5);}
    return {position,target,kind};
}
std::shared_ptr<const LandscapeAtlas> World::landscape() const {
    const bool landforms=modules_.enabled(Module::Landforms);
    if(landscape_ && landscapeLandforms_==landforms) return landscape_;
    struct Entry {Config config;bool landforms;std::shared_ptr<const LandscapeAtlas> atlas;};
    static std::mutex mutex;static std::vector<Entry> cache;const std::lock_guard lock(mutex);
    for(const auto& e:cache) if(e.config==config_ && e.landforms==landforms) {landscape_=e.atlas;landscapeLandforms_=landforms;return landscape_;}
    landscape_=std::make_shared<LandscapeAtlas>(*this);landscapeLandforms_=landforms;
    if(cache.size()>=3) cache.erase(cache.begin());
    cache.push_back({config_,landforms,landscape_});return landscape_;
}
} // namespace planet

#include "planet/fauna.hpp"
#include "planet/landscape.hpp"
#include "planet/rivers.hpp"
#include <stdexcept>

namespace planet {
std::string_view deerActivityName(DeerActivity a) {
    constexpr std::array<std::string_view,5> names{"grazing","walking to water","resting","drinking","fleeing"};
    return names.at(static_cast<std::size_t>(a));
}
namespace {
struct Ground {double bed=0,level=0;bool cave=false,ocean=false;};
Ground ground(const World& w,Vec3 p) {
    const auto r=w.relief(p);Ground g{r.elevation,r.inland ? r.waterLevel : r.elevation,false,false};
    if(w.modules().enabled(Module::Rivers)) {const auto river=w.waterNetwork()->sample(p);if(river.wet) g.level=std::max(g.level,river.level);}
    if(w.modules().enabled(Module::Ocean) && g.bed<w.config().seaLevel) {g.level=std::max(g.level,w.config().seaLevel);g.ocean=true;}
    if(const auto cave=w.caveSite(p)) g.cave=cave->protectedGround(p);
    return g;
}
Vec3 offset(Vec3 p,Vec3 tangent,double metres,double radius) {return normalized(p+tangent*(metres/radius));}
double distance(Vec3 a,Vec3 b,double r) {return length(a-b)*r;}
Vec3 toward(Vec3 a,Vec3 b) {const auto v=b-a*dot(a,b);return length(v)>1e-12 ? normalized(v) : north(a);}
bool clear(Vec3 p,double elevation,const std::vector<SurfaceObject>& objects,double radius) {
    for(const auto& o:objects) if(o.collisionRadius()>.05 && o.elevation+o.collisionHeight()>elevation+.25 &&
        distance(p,o.up,radius)<o.collisionRadius()+.65) return false;
    return true;
}
bool dry(const Ground& g) {return !g.cave && !g.ocean && g.level-g.bed<.16;}
std::optional<std::pair<Vec3,Vec3>> shore(const World& w,Vec3 wet,Vec3 outward,double reach) {
    const double radius=w.config().radius;auto a=ground(w,wet);
    if(a.ocean || a.cave || a.level-a.bed<.04) return {};
    Vec3 land{};bool found=false;
    for(double d=2;d<=reach;d*=1.5) {
        land=offset(wet,outward,d,radius);const auto g=ground(w,land);
        if(!g.cave && !g.ocean && g.bed>a.level+.03) {found=true;break;}
    }
    if(!found) return {};
    auto water=wet;
    for(int i=0;i<18;++i) {
        const auto m=normalized(land+water);const auto g=ground(w,m);
        if(g.bed>a.level+.025) land=m;else water=m;
    }
    const auto sip=offset(land,-outward,1.0,radius),bank=offset(land,outward,.65,radius);
    const auto b=ground(w,bank),s=ground(w,sip);
    if(!dry(b) || b.bed-s.level>.45 || s.ocean || s.level-s.bed<.015) return {};
    return std::pair{bank,sip};
}
std::optional<DeerHabitat> habitat(const World& w,std::uint64_t id,Vec3 wet,Vec3 out,double reach) {
    const auto edge=shore(w,wet,out,reach);if(!edge) return {};
    const auto home=offset(edge->first,out,32,w.config().radius);
    const auto climate=w.climatology(home);
    if(climate.temperature<275 || climate.temperature>302 || climate.precipitation<350 ||
        climate.elevation>2600 || climate.biome==Biome::Desert || climate.biome==Biome::Ice || climate.biome==Biome::Rainforest) return {};
    const auto h=ground(w,home);if(!dry(h)) return {};
    for(const auto axis:{out,-out,east(home),-east(home)}) {
        const auto g=ground(w,offset(home,axis,12,w.config().radius));
        if(!dry(g) || std::abs(g.bed-h.bed)>5) return {};
    }
    return DeerHabitat{id,home,edge->first,edge->second,out};
}
std::optional<DeerHabitat> patchHabitat(const World& w,const LandscapePatch& p) {
    if(!inlandLandform(p.kind)) return {};
    for(int i=0;i<16;++i) {
        const double a=tau*(i/16.+randomUnit(p.id));const auto out=normalized(p.right*std::cos(a)+p.forward*std::sin(a));
        const auto wet=p.directionAt(std::cos(a)*p.width*.75,std::sin(a)*p.length*.75);
        if(auto h=habitat(w,hash64(p.id^0xdee471ULL),wet,out,p.width)) return h;
    }
    return {};
}
std::optional<DeerHabitat> riverHabitat(const World& w,std::size_t r,std::size_t s) {
    const auto point=w.waterNetwork()->point(r,s,.5);const auto id=hash64(w.config().seed^hash64(r)^hash64(s+0xdee4ULL));
    for(double sign:{1.,-1.}) if(auto h=habitat(w,id,point.up,point.left*sign,point.width+300)) return h;
    return {};
}
bool path(const World& w,Vec3 start,Vec3 end,double startHeight,const std::vector<SurfaceObject>& obstacles) {
    const double span=distance(start,end,w.config().radius);const int n=std::max(1,static_cast<int>(std::ceil(span)));
    double previous=startHeight;
    for(int j=1;j<=n;++j) {
        const auto p=normalized(start*(1-j/static_cast<double>(n))+end*(j/static_cast<double>(n)));const auto g=ground(w,p);
        if(!dry(g) || std::abs(g.bed-previous)>span/n*.6+.035 || !clear(p,g.bed,obstacles,w.config().radius)) return false;
        previous=g.bed;
    }
    return true;
}
void targets(DeerHerd& herd,const World& w,bool drinking) {
    const double radius=w.config().radius;
    for(std::size_t i=0;i<herd.members.size();++i) {
        auto& d=herd.members[i];
        if(drinking) {
            const auto side=normalized(cross(herd.habitat.outward,herd.habitat.bank));
            const auto wet=offset(herd.habitat.water,side,(static_cast<double>(i)-(herd.members.size()-1)*.5)*2.7,radius);
            const auto edge=shore(w,offset(wet,-herd.habitat.outward,3,radius),herd.habitat.outward,40);
            d.target=edge ? edge->first : herd.habitat.bank;
        } else {
            const auto key=hash64(d.id+herd.decisions*73);const double a=tau*randomUnit(key),extent=8+15*randomUnit(key+1);
            const auto target=offset(herd.habitat.home,east(herd.habitat.home)*std::cos(a)+north(herd.habitat.home)*std::sin(a),extent,radius);
            d.target=dry(ground(w,target)) ? target : herd.habitat.home;
        }
    }
}
}
DeerHabitat findDeerHabitat(const World& w,Vec3 near) {
    if(!w.modules().enabled(Module::Fauna)) throw std::runtime_error("Enable Fauna, Terrain and Biomes to visit deer");
    if(w.modules().enabled(Module::InlandWaters)) for(auto kind:{Landform::Pond,Landform::Lake,Landform::Wetland}) {
        try {const auto place=w.findLandmark(kind,near);if(const auto p=w.landscape()->patch(place.position)) if(const auto h=patchHabitat(w,*p)) return *h;} catch(const std::runtime_error&) {}
    }
    if(w.modules().enabled(Module::Rivers)) {
        for(auto kind:{RiverKind::Stream,RiverKind::River}) {
            const auto site=w.waterNetwork()->find(kind,near);
            for(const auto [r,s]:w.waterNetwork()->nearby(site.center,300)) if(const auto h=riverHabitat(w,r,s)) return *h;
        }
    }
    throw std::runtime_error("No suitable deer habitat with fresh drinking water was found");
}
void Fauna::synchronize(const World& w) {
    unsigned geometry=0;for(auto m:{Module::Terrain,Module::Landforms,Module::Caves,Module::Rivers,Module::InlandWaters,Module::Biomes,Module::SurfaceDetail})
        if(w.modules().enabled(m)) geometry|=1U<<static_cast<unsigned>(m);
    if(!initialized_ || !(config_==w.config()) || geometry!=geometry_) {
        config_=w.config();geometry_=geometry;initialized_=true;herds_.clear();surveyed_={};pending_=0;elapsed_=0;steps_=0;
    }
    enabled_=w.modules().enabled(Module::Fauna);
}
bool Fauna::focus(const World& w,const DeerHabitat& site) {
    synchronize(w);if(!enabled_) return false;
    for(auto& h:herds_) if(h.habitat.id==site.id) {h.lastSeen=++visit_;return true;}
    DeerHerd herd;herd.habitat=site;herd.lastSeen=++visit_;herd.remaining=35+40*randomUnit(site.id+1);herd.thirst=.5+.3*randomUnit(site.id+2);
    herd.obstacles=surfaceObjects(w,site.home,180,false);
    std::erase_if(herd.obstacles,[](const auto& o){return o.collisionRadius()<=.05;});
    const unsigned count=3+static_cast<unsigned>(hash64(site.id)%5);
    for(unsigned i=0;i<count;++i) {
        Deer d;d.id=hash64(site.id+i+17);d.size=.82+.23*randomUnit(d.id);d.male=randomUnit(d.id+1)<.32;
        bool placed=false;
        for(unsigned trial=0;trial<40;++trial) {
            const auto a=tau*randomUnit(d.id+trial*31);const double r=3+10*randomUnit(d.id+trial*31+1);
            d.up=offset(site.home,east(site.home)*std::cos(a)+north(site.home)*std::sin(a),r,config_.radius);
            const auto g=ground(w,d.up);if(!dry(g) || !clear(d.up,g.bed,herd.obstacles,config_.radius)) continue;
            bool separate=true;for(const auto& other:herd.members) if(distance(d.up,other.up,config_.radius)<2.3) separate=false;
            if(!separate) continue;
            d.elevation=d.previousElevation=g.bed;placed=true;break;
        }
        if(!placed) return false;
        d.previous=d.up;d.heading=d.previousHeading=toward(d.up,site.bank);d.target=d.up;
        d.lowerHead=d.previousHead=.8;herd.members.push_back(d);
    }
    if(herds_.size()>=cacheLimit) herds_.erase(std::min_element(herds_.begin(),herds_.end(),[](const auto& a,const auto& b){return a.lastSeen<b.lastSeen;}));
    herds_.push_back(std::move(herd));return true;
}
void Fauna::discover(const World& w,Vec3 observer) {
    if(length(surveyed_)>.5 && distance(observer,surveyed_,config_.radius)<180) return;
    surveyed_=observer;
    if(w.modules().enabled(Module::InlandWaters)) {
        if(const auto p=w.landscape()->patch(observer);p && inlandLandform(p->kind) && distance(p->anchor,observer,config_.radius)<p->width*1.7+activeRadius) {
            if(const auto h=patchHabitat(w,*p);h && distance(h->home,observer,config_.radius)<activeRadius+100) (void)focus(w,*h);
        }
    }
    if(w.modules().enabled(Module::Rivers)) {
        unsigned checked=0;
        for(const auto [r,s]:w.waterNetwork()->nearby(observer,activeRadius)) {
            if(distance(w.waterNetwork()->point(r,s,.5).up,observer,config_.radius)>activeRadius) continue;
            if(const auto h=riverHabitat(w,r,s)) (void)focus(w,*h);
            if(++checked>=2) break;
        }
    }
}
void Fauna::advance(const World& w,Vec3 observer,double seconds,double altitude,bool observable,bool threat) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>3600 || !std::isfinite(altitude) || !std::isfinite(length(observer)) || length(observer)<.5)
        throw std::invalid_argument("Invalid fauna time or observer");
    observer=normalized(observer);synchronize(w);
    if(!enabled_ || !observable || altitude-w.surface(observer)>300) {for(auto& h:herds_) h.active=false;pending_=0;return;}
    discover(w,observer);
    std::vector<std::pair<double,std::size_t>> order;
    for(std::size_t i=0;i<herds_.size();++i) {
        auto& h=herds_[i];h.active=false;double d=distance(h.habitat.home,observer,config_.radius);
        for(const auto& deer:h.members) d=std::min(d,distance(deer.up,observer,config_.radius));
        if(d<activeRadius) order.emplace_back(d,i);
    }
    std::sort(order.begin(),order.end());
    for(std::size_t i=0;i<std::min(maximumHerds,order.size());++i) {auto& h=herds_[order[i].second];h.active=true;h.lastSeen=++visit_;}
    pending_+=seconds;
    while(pending_+1e-9>=fixedStep) {tick(w,observer,altitude,threat);pending_=std::max(0.,pending_-fixedStep);elapsed_+=fixedStep;++steps_;}
}
void Fauna::tick(const World& w,Vec3 observer,double altitude,bool threat) {
    const double radius=config_.radius,dt=fixedStep;
    for(auto& herd:herds_) if(herd.active) {
        Vec3 center{};bool alarm=false;double nearest=1e30;
        for(const auto& d:herd.members) {center+=d.up;const double range=std::hypot(distance(d.up,observer,radius),altitude-d.elevation-1.0);nearest=std::min(nearest,range);}
        center=normalized(center);alarm=threat && nearest<42;
        if(alarm) {herd.alarm=14;herd.activity=DeerActivity::Fleeing;}
        else if(!threat || nearest>95) herd.alarm=std::max(0.,herd.alarm-dt);
        herd.remaining-=dt;herd.thirst=std::min(1.,herd.thirst+dt/420);
        if(herd.activity==DeerActivity::Fleeing && herd.alarm<=0) {herd.activity=DeerActivity::Grazing;herd.remaining=25;++herd.decisions;targets(herd,w,false);}
        if(herd.activity!=DeerActivity::Fleeing && herd.remaining<=0) {
            ++herd.decisions;
            if(herd.activity==DeerActivity::Walking || herd.activity==DeerActivity::Drinking) {
                herd.activity=DeerActivity::Grazing;herd.remaining=40;targets(herd,w,false);
            } else if(herd.thirst>.72) {
                herd.activity=DeerActivity::Walking;herd.remaining=150;targets(herd,w,true);
            } else if(herd.activity==DeerActivity::Resting) {
                herd.activity=DeerActivity::Grazing;herd.remaining=40+35*randomUnit(herd.habitat.id+herd.decisions);targets(herd,w,false);
            } else {herd.activity=DeerActivity::Resting;herd.remaining=25+30*randomUnit(herd.habitat.id+herd.decisions);}
        }
        const auto old=herd.members;unsigned drinking=0;
        for(auto& d:herd.members) {
            d.previous=d.up;d.previousElevation=d.elevation;d.previousHeading=d.heading;d.previousStride=d.stride;d.previousHead=d.lowerHead;d.previousFold=d.foldLegs;
            auto activity=herd.activity;Vec3 desired=toward(d.up,d.target);double targetSpeed=0;
            if(activity==DeerActivity::Fleeing) {
                desired=toward(observer,d.up)+toward(d.up,center)*clamp((distance(d.up,center,radius)-12)/28,0.,.65);
                targetSpeed=10+2*randomUnit(d.id+4);
            } else if(activity==DeerActivity::Walking || activity==DeerActivity::Drinking) {
                if(distance(d.up,d.target,radius)<1.0) {
                    const auto wet=ground(w,offset(d.up,-herd.habitat.outward,1.5,radius));
                    if(!wet.ocean && wet.level-wet.bed>.01 && d.elevation-wet.level<.5) {activity=DeerActivity::Drinking;++drinking;desired=-herd.habitat.outward;}
                    else activity=DeerActivity::Walking;
                } else {activity=DeerActivity::Walking;targetSpeed=1.1;}
            } else if(activity==DeerActivity::Grazing) targetSpeed=distance(d.up,d.target,radius)>1.8 ? .42 : 0;
            Vec3 separation{};
            for(const auto& other:old) if(other.id!=d.id) {
                const double gap=distance(d.up,other.up,radius);
                if(gap<3) separation+=toward(other.up,d.up)*((3-gap)/3);
            }
            desired+=separation*.9;desired=normalized(desired-d.up*dot(desired,d.up));
            const double turn=std::atan2(dot(cross(d.heading,desired),d.up),dot(d.heading,desired));
            const auto heading=normalized(rotate(d.heading,d.up,clamp(turn,-2.8*dt,2.8*dt)));
            const double speed=d.speed+clamp(targetSpeed-d.speed,-8*dt,4*dt);bool moved=false;
            if(speed>.005) for(double angle:{0.,.55,-.55,1.1,-1.1,1.7,-1.7}) {
                const auto axis=rotate(heading,d.up,angle);const auto next=offset(d.up,axis,speed*dt,radius);
                const auto ahead=offset(d.up,axis,std::max(speed*dt,1.5),radius);
                if(!path(w,d.up,ahead,d.elevation,herd.obstacles)) continue;
                d.up=next;d.elevation=ground(w,next).bed;d.heading=toward(d.previous,next);d.speed=speed;d.stride+=speed*dt/(speed>4 ? 2.5 : 1.4);moved=true;break;
            }
            if(!moved) {d.speed=0;d.heading=heading;}
            d.activity=activity;
            const double head=(activity==DeerActivity::Drinking || (activity==DeerActivity::Grazing && d.speed<.6)) ? 1 : 0;
            const double folded=activity==DeerActivity::Resting && d.speed<.1 ? 1 : 0;
            d.lowerHead+=clamp(head-d.lowerHead,-dt*2.5,dt*1.5);d.foldLegs+=clamp(folded-d.foldLegs,-dt*3,dt*.7);
        }
        if(drinking>0) {
            herd.thirst=std::max(0.,herd.thirst-dt*.07*drinking/static_cast<double>(herd.members.size()));
            if(drinking==herd.members.size() && herd.activity==DeerActivity::Walking) {herd.activity=DeerActivity::Drinking;herd.remaining=14;}
        }
    }
}
std::vector<DeerPose> Fauna::visible(Vec3 observer,double radius) const {
    std::vector<DeerPose> result;if(!enabled_) return result;
    const double t=clamp(pending_/fixedStep,0.,1.);
    for(const auto& h:herds_) if(h.active) for(const auto& d:h.members) {
        if(distance(d.up,observer,config_.radius)>radius) continue;
        result.push_back({d.id,h.habitat.id,normalized(d.previous*(1-t)+d.up*t),normalized(d.previousHeading*(1-t)+d.heading*t),
            mix(d.previousElevation,d.elevation,t),mix(d.previousStride,d.stride,t),mix(d.previousHead,d.lowerHead,t),mix(d.previousFold,d.foldLegs,t),d.size,d.speed,d.male,d.activity});
    }
    return result;
}
} // namespace planet

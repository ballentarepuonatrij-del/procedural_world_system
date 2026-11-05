#include "planet/rivers.hpp"
#include <queue>
#include <numeric>
#include <stdexcept>
#include <atomic>

namespace planet {
namespace {
constexpr double minimumSlope=2e-5;
int wrap(int x,int n) {return (x%n+n)%n;}
double distance(Vec3 a,Vec3 b,double radius) {return length(a-b)*radius;}
double referenceRain(Vec3 p,std::uint64_t seed) {
    const double lat=std::abs(degrees(latitude(p)));
    return (120+2100*std::exp(-std::pow(lat/16,2))+1000*std::exp(-std::pow((lat-52)/17,2)))*
        (.75+.5*Noise(seed).value(p*18));
}
}
RiverSection riverSection(double q,double slope,double n) {
    if(!std::isfinite(q) || q<0 || !std::isfinite(slope) || slope<0 || !std::isfinite(n) || n<=0)
        throw std::invalid_argument("Invalid river hydraulics");
    if(q==0) return {};
    const double width=std::max(1.2,4.5*std::pow(q,.45));
    const double frictionSlope=clamp(slope,minimumSlope,.04);
    const auto conveyance=[&](double d) {const double area=.8*width*d,perimeter=.6*width+2*std::hypot(.2*width,d);return area*std::pow(area/perimeter,2./3)*std::sqrt(frictionSlope)/n;};
    double low=0,high=1;while(conveyance(high)<q) high*=2;
    for(int i=0;i<36;++i) {const double mid=(low+high)/2;if(conveyance(mid)<q) low=mid;else high=mid;}
    const double depth=std::max((low+high)/2,q/(.8*width*6));
    return {width,depth,q/(.8*width*depth)};
}
std::optional<RiverKind> riverKind(std::string_view name) {
    if(name=="stream") return RiverKind::Stream;
    if(name=="river") return RiverKind::River;
    if(name=="major") return RiverKind::Major;
    return {};
}
RiverNetwork::RiverNetwork(const World& world) : base_(world) {
    static std::atomic<std::uint64_t> nextIdentity{1};
    cacheIdentity_=nextIdentity.fetch_add(1,std::memory_order_relaxed);
    base_.waterNetwork_.reset();base_.modules().set(Module::Rivers,false);
    const auto& config=base_.config();const double radius=config.radius,sea=config.seaLevel;
    nodes_.resize(columns*rows);std::vector<bool> visited(nodes_.size());std::vector<int> order;
    using Queue=std::pair<double,int>;std::priority_queue<Queue,std::vector<Queue>,std::greater<>> queue;
    const auto index=[](int x,int y){return y*columns+wrap(x,columns);};
    double lowest=1e30;int lowestId=0;
    for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
        const int id=index(x,y);auto& node=nodes_[static_cast<std::size_t>(id)];
        node.up=direction(-pi/2+pi*(y+.5)/rows,-pi+tau*(x+.5)/columns);
        node.elevation=base_.naturalElevation(node.up);node.ocean=node.elevation<=sea;
        node.area=radius*radius*tau/columns*(std::sin(-pi/2+pi*(y+1)/rows)-std::sin(-pi/2+pi*y/rows));
        node.runoff=node.ocean ? 0 : referenceRain(node.up,config.seed)*.001*.32*node.area/config.year;
        node.discharge=node.runoff;totalRunoff_+=node.runoff;
        if(node.elevation<lowest) {lowest=node.elevation;lowestId=id;}
        if(node.ocean) {node.filled=sea;node.level=sea;visited[static_cast<std::size_t>(id)]=true;queue.emplace(sea,id);}
    }
    const double outlet=queue.empty() ? lowest : sea;
    if(queue.empty()) {visited[static_cast<std::size_t>(lowestId)]=true;nodes_[static_cast<std::size_t>(lowestId)].filled=lowest;nodes_[static_cast<std::size_t>(lowestId)].level=lowest;queue.emplace(lowest,lowestId);}
    // Priority flood assigns every cell an earlier downstream receiver, including
    // depressions. Distances define a strictly descending conditioned profile.
    while(!queue.empty()) {
        const auto [height,id]=queue.top();queue.pop();order.push_back(id);const int x=id%columns,y=id/columns;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if((!dx && !dy) || y+dy<0 || y+dy>=rows) continue;
            const int next=index(x+dx,y+dy);if(visited[static_cast<std::size_t>(next)]) continue;
            auto& n=nodes_[static_cast<std::size_t>(next)];const auto& parent=nodes_[static_cast<std::size_t>(id)];
            const double span=distance(n.up,parent.up,radius);
            n.downstream=id;n.distance=parent.distance+span;n.filled=std::max(n.elevation,height+minimumSlope*span);
            n.level=std::max(outlet+minimumSlope*n.distance,n.elevation-2);
            visited[static_cast<std::size_t>(next)]=true;queue.emplace(n.filled,next);
        }
    }
    for(auto it=order.rbegin();it!=order.rend();++it) {
        const auto& n=nodes_[static_cast<std::size_t>(*it)];
        if(n.downstream>=0) {auto& parent=nodes_[static_cast<std::size_t>(n.downstream)];parent.discharge+=n.discharge;parent.area+=n.area;}
        else oceanDischarge_+=n.discharge;
    }
    // Resolve headwaters first. Every reach contributes its upper bound to the
    // downstream junction; a later pass shares the final junction level exactly.
    for(auto it=order.rbegin();it!=order.rend();++it) {
        auto& n=nodes_[static_cast<std::size_t>(*it)];if(n.downstream<0 || n.ocean || n.discharge<.02) continue;
        auto& parent=nodes_[static_cast<std::size_t>(n.downstream)];
        const double lengthMetres=distance(n.up,parent.up,radius);const Vec3 middle=normalized(n.up+parent.up);
        const Vec3 left=normalized(cross(middle,parent.up-n.up));
        // A bounded valley search bends edges away from high ridges while keeping
        // the global drainage graph intact. These are initial geological shapes.
        double bend=0,best=1e30;
        for(double fraction:{-.12,-.06,0.,.06,.12}) {
            double cost=0;
            for(int k=1;k<8;++k) {const double t=k/8.;const auto p=normalized(n.up*(1-t)+parent.up*t+left*(fraction*lengthMetres*std::sin(pi*t)/radius));cost+=base_.naturalElevation(p);}
            cost+=std::abs(fraction)*1500;if(cost<best) {best=cost;bend=fraction;}
        }
        const int count=std::max(2,static_cast<int>(std::ceil(lengthMetres/3000)));
        RiverReach reach;reach.node=static_cast<std::size_t>(*it);reach.points.reserve(static_cast<std::size_t>(count+1));
        double height=n.level,along=0;
        for(int k=0;k<=count;++k) {
            const double t=static_cast<double>(k)/count;
            const auto p=normalized(n.up*(1-t)+parent.up*t+left*(bend*lengthMetres*std::sin(pi*t)/radius));
            const double step=k ? distance(p,reach.points.back().up,radius) : 0;along+=step;
            const double floor=outlet+minimumSlope*mix(n.distance,parent.distance,t);
            if(k) height=std::max(floor,std::min(height-minimumSlope*step,base_.naturalElevation(p)-2));
            const double q=std::max(.02,n.discharge-n.runoff+n.runoff*t);
            reach.points.push_back({p,left,height,q,0,0,along});
        }
        parent.level=std::min(parent.level,reach.points.back().level);reaches_.push_back(std::move(reach));
    }
    for(std::size_t ri=0;ri<reaches_.size();++ri) {
        auto& reach=reaches_[ri];auto& points=reach.points;const auto& node=nodes_[reach.node];
        points.front().level=node.level;points.back().level=nodes_[static_cast<std::size_t>(node.downstream)].level;
        for(std::size_t j=0;j<points.size();++j) {
            const auto before=j ? j-1 : j,after=std::min(j+1,points.size()-1);
            const double span=distance(points[before].up,points[after].up,radius);
            const double slope=std::max(minimumSlope,(points[before].level-points[after].level)/std::max(1.,span));
            const auto section=riverSection(points[j].discharge,slope);
            points[j].width=section.width;points[j].depth=section.depth;
            points[j].left=normalized(cross(points[j].up,points[after].up-points[before].up));
            if(j+1<points.size()) {
                const auto key=bin(normalized(points[j].up+points[j+1].up));
                buckets_[key[1]*binsX+key[0]].push_back(segments_.size());segments_.push_back({ri,j});
            }
        }
    }
    buildCirculation();
}
std::array<int,2> RiverNetwork::bin(Vec3 p) const {
    return {wrap(static_cast<int>(std::floor((longitude(p)+pi)*binsX/tau)),binsX),clamp(static_cast<int>(std::floor((latitude(p)+pi/2)*binsY/pi)),0,binsY-1)};
}
std::vector<std::size_t> RiverNetwork::candidates(Vec3 up,double radius) const {
    const auto b=bin(up);std::vector<std::size_t> found;
    const int dy=std::max(1,static_cast<int>(std::ceil(radius/(base_.config().radius*pi/binsY))));
    const int dx=std::min(binsX/2,1+static_cast<int>(std::ceil(radius/(base_.config().radius*tau/binsX*std::max(.02,std::cos(latitude(up)))))));
    for(int y=std::max(0,b[1]-dy);y<=std::min(binsY-1,b[1]+dy);++y) for(int x=b[0]-dx;x<=b[0]+dx;++x) {
        const auto it=buckets_.find(y*binsX+wrap(x,binsX));if(it!=buckets_.end()) found.insert(found.end(),it->second.begin(),it->second.end());
    }
    return found;
}
RiverPoint RiverNetwork::point(std::size_t reach,std::size_t segment,double t) const {
    const auto& points=reaches_.at(reach).points;const auto& a=points.at(segment);const auto& b=points.at(segment+1);
    RiverPoint p;p.up=normalized(a.up*(1-t)+b.up*t);p.left=normalized(a.left*(1-t)+b.left*t);
    p.level=mix(a.level,b.level,t);p.discharge=mix(a.discharge,b.discharge,t);p.width=std::max(1.2,4.5*std::pow(p.discharge,.45));
    // Interpolate wetted area, not depth: Q/A stays between endpoint speeds.
    p.depth=mix(a.width*a.depth,b.width*b.depth,t)/p.width;p.along=mix(a.along,b.along,t);
    // Continuous, modest channel meanders. End taper keeps every confluence joined.
    const double taper=smoothstep(0,500,p.along)*(1-smoothstep(points.back().along-500,points.back().along,p.along));
    const double amplitude=clamp(p.width*.7,3.0,90.0)*taper,wavelength=400+p.width*18;
    const double phase=tau*randomUnit(base_.config().seed+reaches_[reach].node);
    p.up=normalized(p.up+p.left*(amplitude*std::sin(tau*p.along/wavelength+phase)/base_.config().radius));return p;
}
RiverSample RiverNetwork::sample(Vec3 up,double natural) const {
    // Atlases are immutable and shared by the simulation and mesh worker.
    // Only the single-query acceleration cache belongs to the calling thread.
    struct Cache {std::uint64_t identity=0;Vec3 up{};double natural=0;RiverSample sample;};
    static thread_local Cache cache;
    up=normalized(up);
    if(cache.identity==cacheIdentity_ && up.x==cache.up.x && up.y==cache.up.y && up.z==cache.up.z &&
       (!std::isfinite(natural) || natural==cache.natural)) return cache.sample;
    RiverSample best;double score=1e30;const double radius=base_.config().radius;
    for(const auto id:candidates(up,8000)) {
        const auto s=segments_[id];const auto& pts=reaches_[s.reach].points;const auto& a=pts[s.point];const auto& b=pts[s.point+1];
        const auto delta=b.up-a.up;double t=clamp(dot(up-a.up,delta)/dot(delta,delta),0.0,1.0);
        const double approximateDistance=distance(up,normalized(a.up*(1-t)+b.up*t),radius);
        if(approximateDistance>6100) continue;
        if(!std::isfinite(natural)) natural=base_.naturalElevation(up);
        const double valleyBound=std::min(6000.,std::max(45.,std::max(a.width,b.width)*2+std::max(std::abs(natural-a.level),std::abs(natural-b.level))*4));
        if(approximateDistance>valleyBound+100) continue;
        // Project onto the curved centreline, not just its straight skeleton.
        for(int k=0;k<3;++k) {
            const auto p=point(s.reach,s.point,t),lo=point(s.reach,s.point,clamp(t-.002,0.,1.)),hi=point(s.reach,s.point,clamp(t+.002,0.,1.));
            const auto derivative=(hi.up-lo.up)/(std::min(1.,t+.002)-std::max(0.,t-.002));
            t=clamp(t+dot(up-p.up,derivative)/std::max(1e-20,dot(derivative,derivative)),0.0,1.0);
        }
        const auto p=point(s.reach,s.point,t);const double away=distance(up,p.up,radius);if(away>6000) continue;
        if(!std::isfinite(natural)) natural=base_.naturalElevation(up);
        const double valley=std::min(6000.,std::max(45.,p.width*2+std::abs(natural-p.level)*4));
        if(away>valley) continue;
        const double bottom=p.level-p.depth*(1-smoothstep(.3*p.width,.5*p.width,away));
        const double bed=away<=.5*p.width ? bottom : mix(p.level+(away-.5*p.width)*.12,natural,smoothstep(.5*p.width,valley,away));
        const double rank=away<=.5*p.width ? -10000+bed : bed-natural;
        if(rank>=score) continue;
        score=rank;
        const auto lo=point(s.reach,s.point,std::max(0.,t-.001)),hi=point(s.reach,s.point,std::min(1.,t+.001));
        const auto flow=normalized(hi.up-lo.up);const double speed=p.discharge/(.8*p.width*p.depth);
        best={true,away<.5*p.width,p.up,normalized(flow-up*dot(flow,up)),away,dot(up-p.up,p.left)*radius,p.level,bed,
            std::max(0.,p.level-bed),p.width,p.discharge,speed,p.along,std::max(minimumSlope,(a.level-b.level)/std::max(1.,b.along-a.along))};
    }
    cache={cacheIdentity_,up,natural,best};return best;
}
std::vector<std::pair<std::size_t,std::size_t>> RiverNetwork::nearby(Vec3 up,double radius) const {
    std::vector<std::pair<std::size_t,std::size_t>> result;
    for(const auto id:candidates(up,radius+4000)) {const auto s=segments_[id];const auto& p=reaches_[s.reach].points;
        if(distance(up,normalized(p[s.point].up+p[s.point+1].up),base_.config().radius)<radius+4000) result.emplace_back(s.reach,s.point);}
    return result;
}
RiverSite RiverNetwork::find(RiverKind kind,Vec3 near) const {
    const double target=kind==RiverKind::Stream ? 1.8 : kind==RiverKind::River ? 16 : 100;
    double best=1e30;RiverSite site;bool found=false;
    for(std::size_t r=0;r<reaches_.size();++r) {
        const auto& reach=reaches_[r];const auto& pts=reach.points;
        for(std::size_t j=0;j+1<pts.size();j+=std::max<std::size_t>(1,pts.size()/12)) {
            const double desiredQ=std::pow(target/4.5,1/.45);
            const double t=j==0 ? clamp((desiredQ-pts[j].discharge)/std::max(1e-12,pts[j+1].discharge-pts[j].discharge),0.,1.) : .5;
            const auto p=point(r,j,t);
            if(p.level<base_.config().seaLevel+5 || std::abs(latitude(p.up))>radians(70)) continue;
            const double slope=(pts[j].level-pts[j+1].level)/std::max(1.,pts[j+1].along-pts[j].along);
            if(slope>.025) continue;
            const double score=std::abs(std::log(p.width/target))*8+length(p.up-near)+.0003*std::abs(base_.naturalElevation(p.up)-p.level);
            if(score>=best) continue;
            const auto forward=normalized(pts[j+1].up-pts[j].up);const auto right=normalized(cross(forward,p.up));
            site={normalized(p.up+right*((p.width*.5+5)/base_.config().radius)),p.up,normalized(forward-p.up*dot(forward,p.up)),p.width,p.discharge};best=score;found=true;
        }
    }
    if(!found) throw std::runtime_error("No river site on this planet");
    return site;
}
void RiverNetwork::buildCirculation() {
    const std::size_t size=nodes_.size();streamfunction_.assign(size,0);std::vector<bool> active(size);
    std::vector<std::array<double,5>> coefficients(size);std::vector<std::array<int,4>> neighbours(size);
    std::vector<double> rhs(size),residual(size),search(size),product(size);
    const double radius=base_.config().radius,dlat=pi/rows,dlon=tau/columns;
    const auto stress=[](double lat){const double wind=-7*std::cos(3*lat)*std::cos(lat)-3*std::exp(-std::pow((std::abs(lat)-radians(80))/radians(10),2));return 1.225*.0014*wind*std::abs(wind);};
    for(int y=1;y<rows-1;++y) for(int x=0;x<columns;++x) {
        const auto i=static_cast<std::size_t>(y*columns+x);neighbours[i]={y*columns+wrap(x-1,columns),y*columns+wrap(x+1,columns),(y-1)*columns+x,(y+1)*columns+x};
        active[i]=nodes_[i].ocean;for(int j:neighbours[i]) active[i]=active[i] && nodes_[static_cast<std::size_t>(j)].ocean;
        if(!active[i]) continue;
        const double lat=-pi/2+(y+.5)*dlat,c=std::cos(lat);
        const double ew=dlat/(c*dlon),south=std::cos(lat-dlat/2)*dlon/dlat,north=std::cos(lat+dlat/2)*dlon/dlat;
        coefficients[i]={2*ew+south+north,ew,ew,south,north};
        const double curl=-(stress(lat+dlat/2)*std::cos(lat+dlat/2)-stress(lat-dlat/2)*std::cos(lat-dlat/2))/(radius*c*dlat);
        rhs[i]=curl*radius*radius*c*dlat*dlon/(1025*50*(1./86400));
    }
    residual=rhs;search=rhs;
    const auto dotVector=[](const auto& a,const auto& b){return std::inner_product(a.begin(),a.end(),b.begin(),0.);};
    const double original=dotVector(rhs,rhs);double rr=original;
    for(int iteration=0;iteration<800 && rr>std::max(1e-24,original*1e-16);++iteration) {
        for(std::size_t i=0;i<size;++i) {product[i]=0;if(!active[i]) continue;const auto& c=coefficients[i];product[i]=c[0]*search[i];for(unsigned j=0;j<4;++j) product[i]-=c[j+1]*search[static_cast<std::size_t>(neighbours[i][j])];}
        const double denominator=dotVector(search,product);if(denominator<=0) break;const double alpha=rr/denominator;
        for(std::size_t i=0;i<size;++i) {streamfunction_[i]+=alpha*search[i];residual[i]-=alpha*product[i];}
        const double next=dotVector(residual,residual),beta=next/rr;for(std::size_t i=0;i<size;++i) search[i]=residual[i]+beta*search[i];rr=next;
    }
    circulationResidual_=original>0 ? std::sqrt(rr/original) : 0;
}
Vec3 RiverNetwork::oceanCurrent(Vec3 up,double below) const {
    up=normalized(up);const double depth=base_.config().seaLevel-base_.naturalElevation(up);if(depth<=0) return {};
    const double lat=latitude(up),gx=(longitude(up)+pi)*columns/tau-.5,gy=(lat+pi/2)*rows/pi-.5;
    const int x=static_cast<int>(std::floor(gx)),y=clamp(static_cast<int>(std::floor(gy)),0,rows-2);const double tx=gx-std::floor(gx),ty=clamp(gy-y,0.,1.);
    const auto at=[&](int dx,int dy){return streamfunction_[static_cast<std::size_t>((y+dy)*columns+wrap(x+dx,columns))];};
    const double eastVelocity=mix(at(0,1)-at(0,0),at(1,1)-at(1,0),tx)/(base_.config().radius*pi/rows);
    const double northVelocity=-mix(at(1,0)-at(0,0),at(1,1)-at(0,1),ty)/(base_.config().radius*std::max(.02,std::cos(lat))*tau/columns);
    return (east(up)*eastVelocity+north(up)*northVelocity)*(smoothstep(0,30,depth)*std::exp(-std::max(0.,below)/80));
}
} // namespace planet

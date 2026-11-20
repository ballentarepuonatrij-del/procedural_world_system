#include "planet/foliage.hpp"
#include "planet/surface_detail.hpp"
#include <stdexcept>
#include <unordered_map>

namespace planet {
bool flexiblePlant(const SurfaceObject& o) {
    return (o.kind==DetailKind::Tree || o.kind==DetailKind::Shrub) && o.height>.03 &&
        o.plant.stage!=GrowthStage::Dead && !plantTraits(o.species).aquatic;
}
FoliageRoot foliageRoot(const SurfaceObject& o,double radius) {
    return {o.id,o.plant.generation,o.up*(radius+o.elevation),o.up,o.height,o.radius,
        o.kind==DetailKind::Shrub ? 0 : o.height*(o.evergreen ? .15 : .38),
        1/(1+.16*o.height),tau*randomUnit(o.id+3061)};
}
Vec3 foliageWindTarget(const FoliageRoot& r,Vec3 wind) {
    wind=wind-r.up*dot(wind,r.up);const double speed=std::min(length(wind),40.);
    return normalized(wind)*(std::min(.32,speed*speed*.0018)*r.compliance);
}
Vec3 bendFoliagePosition(const FoliageVertex& v,Vec3 bend,double phase,double windStrength) {
    const double amount=length(bend);if(amount>.65) bend=bend*(.65/amount);
    const double gust=.78+.18*std::sin(v.phase+phase)+.04*std::sin(v.phase*2+phase*2);
    bend=bend*gust;
    const auto displacement=(bend-Vec3{0,.5*dot(bend,bend),0})*(v.height*v.flex);
    return v.position+displacement+Vec3{.3,1,.2}*(v.flutter*windStrength*std::sin(phase*3+v.leafPhase));
}
void FoliageField::clear() {roots_.clear();states_.clear();contacts_=0;}
void FoliageField::synchronize(std::span<const FoliageRoot> roots) {
    if(roots.size()>maximumFoliagePlants) throw std::invalid_argument("Too many foliage springs");
    std::unordered_map<std::uint64_t,std::size_t> previous;
    for(std::size_t i=0;i<roots_.size();++i) previous.emplace(roots_[i].id,i);
    std::vector<State> next(roots.size());
    for(std::size_t i=0;i<roots.size();++i) if(const auto it=previous.find(roots[i].id);it!=previous.end()) {
        const auto j=it->second;
        if(roots[i].generation==roots_[j].generation && length(roots[i].position-roots_[j].position)<1)
            next[i]=states_[j];
    }
    roots_.assign(roots.begin(),roots.end());states_=std::move(next);contacts_=0;
    for(const auto& s:states_) if(length(s.contact.bend)>.001) ++contacts_;
}
void FoliageField::advance(double modelSeconds,double contactSeconds,Vec3 wind,Vec3 eye,std::span<const GrassCollider> bodies) {
    if(!std::isfinite(modelSeconds) || modelSeconds<0 || modelSeconds>120 || !std::isfinite(contactSeconds) || contactSeconds<0 || contactSeconds>120 || bodies.size()>32 || !std::isfinite(length(wind)) || !std::isfinite(length(eye)))
        throw std::invalid_argument("Invalid foliage forcing");
    for(const auto& b:bodies) if(!std::isfinite(length(b.position)) || !std::isfinite(length(b.previous)) || !std::isfinite(b.radius) || b.radius<=0 || b.radius>5 || !std::isfinite(b.height) || b.height<=0 || b.height>10)
        throw std::invalid_argument("Invalid foliage collider");
    if(modelSeconds==0 && contactSeconds==0) return;
    contacts_=0;
    for(std::size_t i=0;i<roots_.size();++i) {
        const auto& r=roots_[i];auto& s=states_[i];Vec3 target{};
        if(length(r.position-eye)<foliageContactRadius+r.height) for(const auto& body:bodies) {
            auto a=body.previous-r.position,b=body.position-r.position;
            if(length(b-a)>12) a=b; // relocation/fast flight has no artificial swept trail
            const auto delta=b-a-r.up*dot(b-a,r.up),flat=a-r.up*dot(a,r.up);
            const double d2=dot(delta,delta),t=d2>1e-10 ? clamp(-dot(flat,delta)/d2,0.,1.) : 1.;
            const auto center=a+(b-a)*t;const double y=dot(center,r.up);
            if(y>r.height || y+body.height<r.lower) continue;
            auto away=r.up*y-center;const double distance=length(away),reach=r.radius+body.radius;
            if(distance>=reach) continue;
            if(distance<1e-8) away=d2>1e-8 ? cross(r.up,delta) : east(r.up);
            const auto force=normalized(away)*(.58*r.compliance*(1-smoothstep(0,reach,distance)));
            if(length(force)>length(target)) target=force;
        }
        advanceGrassSpring(s.wind,foliageWindTarget(r,wind),modelSeconds,.65+.65*r.compliance);
        advanceGrassSpring(s.contact,target,contactSeconds,.8+.6*r.compliance);
        if(length(s.contact.bend)+length(s.contact.velocity)<1e-7 && length(target)==0) s.contact={};
        if(length(s.contact.bend)>.001) ++contacts_;
    }
}
Vec3 FoliageField::bend(std::size_t slot) const {
    if(slot>=states_.size()) return {};
    auto v=states_[slot].wind.bend+states_[slot].contact.bend;
    const double amount=length(v);if(amount>.65) v=v*(.65/amount);return v;
}
double FoliageField::maximumContact() const {double value=0;for(const auto& s:states_) value=std::max(value,length(s.contact.bend));return value;}
double FoliageField::maximumBend() const {double value=0;for(std::size_t i=0;i<roots_.size();++i) value=std::max(value,length(bend(i)));return value;}
} // namespace planet

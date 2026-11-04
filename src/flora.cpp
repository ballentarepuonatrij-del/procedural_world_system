#include "planet/flora.hpp"
#include <stdexcept>

namespace planet {
const PlantTraits& plantTraits(Species s) {
    static constexpr std::array<PlantTraits,speciesCount> traits{{
        {"None",0,0,1,1,1,false,false,{0,0,0}},
        {"Oak",23,.34,9000,73000,730,false,true,{.23f,.42f,.12f}},
        {"Birch",19,.23,4000,33000,500,false,true,{.39f,.56f,.18f}},
        {"Pine",25,.22,6000,58000,730,false,true,{.12f,.31f,.20f}},
        {"Acacia",12,.52,4500,25000,500,false,true,{.40f,.47f,.16f}},
        {"Tropical tree",30,.32,7000,44000,600,false,true,{.13f,.39f,.14f}},
        {"Juniper",2.4,.65,1000,7300,180,false,true,{.19f,.34f,.26f}},
        {"Heather",.85,.80,360,2900,60,false,false,{.38f,.42f,.20f}},
        {"Meadow grass",.80,.45,90,1095,45,false,false,{.45f,.56f,.18f}},
        {"Fern",1.4,.65,180,1800,90,false,false,{.16f,.47f,.21f}},
        {"Seagrass",1.3,.40,180,1460,60,true,false,{.17f,.48f,.28f}},
        {"Kelp",11,.16,350,1095,60,true,false,{.47f,.39f,.12f}},
        {"Rockweed",1.8,.55,400,2900,90,true,false,{.40f,.34f,.13f}}
    }};
    const auto i=static_cast<unsigned>(s);
    if(i>=traits.size()) throw std::invalid_argument("Unknown plant species");
    return traits[i];
}
std::string_view stageName(GrowthStage s) {
    constexpr std::array<std::string_view,6> names{"seed","seedling","juvenile","mature","senescent","dead"};
    const auto i=static_cast<unsigned>(s); return i<names.size() ? names[i] : "unknown";
}
double plantSuitability(Species species,const Sample& s,double slope,double rock,double sea) {
    const auto& t=plantTraits(species);
    if(species==Species::None || !std::isfinite(s.temperature) || !std::isfinite(s.precipitation) ||
       !std::isfinite(s.elevation) || !std::isfinite(slope) || !std::isfinite(rock) || !std::isfinite(sea)) return 0;
    if(t.aquatic) {
        const double depth=sea-s.elevation;
        if(!s.ocean || depth<.5 || depth>30 || slope>.8) return 0;
        if(species==Species::Seagrass && (s.temperature<277 || s.temperature>303 || slope>.35 || rock>.62 || depth>22)) return 0;
        if(species==Species::Kelp && (s.temperature<276 || s.temperature>294 || rock<.38 || depth<2)) return 0;
        if(species==Species::Rockweed && (s.temperature<276 || s.temperature>300 || rock<.32 || depth>10)) return 0;
        // Beer-Lambert light attenuation with a prescribed clear-water coefficient.
        return clamp(std::exp(-.12*depth)/.6,.05,1.0);
    }
    if(s.ocean || s.elevation<=sea+2 || slope>=.65 || s.temperature<=267 || s.temperature>=315 || s.precipitation<=180) return 0;
    double low=267,high=312,rain=180;
    switch(species) {
        case Species::Oak: low=282;high=301;rain=600;break;
        case Species::Birch: low=275;high=294;rain=400;break;
        case Species::Pine: low=270;high=294;rain=300;break;
        case Species::Acacia: low=288;high=313;rain=180;break;
        case Species::TropicalTree: low=295;high=315;rain=1400;break;
        case Species::Juniper: low=270;high=302;rain=180;break;
        case Species::Heather: low=268;high=293;rain=350;break;
        case Species::Fern: low=280;high=304;rain=800;break;
        default: break;
    }
    if(s.temperature<=low || s.temperature>=high || s.precipitation<rain) return 0;
    const double thermal=clamp(1-std::pow((s.temperature-(low+high)/2)/((high-low)/2),2),.1,1.0);
    const double moisture=clamp(s.precipitation/(rain*1.5),.25,1.0);
    return thermal*moisture*clamp(1-slope,.35,1.0);
}
PlantState plantState(Species species,std::uint64_t id,double days,double suitability,bool young) {
    const auto& t=plantTraits(species);
    if(!std::isfinite(days) || days<0 || days>365250 || !std::isfinite(suitability) || suitability<0 || suitability>1)
        throw std::invalid_argument("Invalid plant age or habitat suitability");
    if(species==Species::None || suitability==0) return {};
    const double initial=young ? std::min(10.0,t.maturityDays*.02)*(.1+.9*randomUnit(id+173)) :
        (randomUnit(id+171)<.18 ? -90*randomUnit(id+172) : t.maturityDays*1.8*randomUnit(id+173));
    const double absolute=days+initial;
    if(absolute<0) return {};
    // A fixed local seed/spore bank renews this site; no spatial dispersal is claimed.
    const double cycle=t.lifespanDays+t.decayDays+90;
    PlantState state; state.generation=static_cast<std::uint64_t>(absolute/cycle);
    state.ageDays=std::fmod(absolute,cycle);
    if(state.ageDays>=t.lifespanDays+t.decayDays) return {state.ageDays,0,0,GrowthStage::Seed,state.generation};
    const double age=std::min(state.ageDays,t.lifespanDays);
    state.size=-std::expm1(-3*age*suitability/t.maturityDays);
    state.foliage=1;
    if(state.ageDays>=t.lifespanDays) {
        state.stage=GrowthStage::Dead; state.foliage=0;
        state.size*=1-(state.ageDays-t.lifespanDays)/t.decayDays;
    } else if(state.ageDays>t.lifespanDays*.9) {
        state.stage=GrowthStage::Senescent;
        state.foliage=clamp((t.lifespanDays-state.ageDays)/(t.lifespanDays*.1),0.0,1.0);
    } else state.stage=state.size<.08 ? GrowthStage::Seedling : (state.size<.70 ? GrowthStage::Juvenile : GrowthStage::Mature);
    return state;
}
void Flora::setRate(double rate) {
    if(!std::isfinite(rate) || rate<1 || rate>30*86400) throw std::invalid_argument("Growth rate must be 1..2592000");
    rate_=rate;
}
void Flora::advanceDays(const World& world,double days) {
    if(!std::isfinite(days) || days<0 || days>365250) throw std::invalid_argument("Growth interval must be 0..365250 days");
    if(world.modules().enabled(Module::PlantGrowth)) days_=std::min(365250.0,days_+days);
}
void Flora::advance(const World& world,double seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>86400) throw std::invalid_argument("Invalid biological timestep");
    advanceDays(world,std::min(365250.0,seconds*rate_/86400));
}
} // namespace planet

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
        {"Rockweed",1.8,.55,400,2900,90,true,false,{.40f,.34f,.13f}},
        {"Oxeye daisy",.75,.32,100,1460,60,false,false,{.25f,.48f,.15f}},
        {"Red clover",.48,.60,85,1095,45,false,false,{.20f,.48f,.19f}},
        {"Field poppy",.85,.28,70,245,30,false,false,{.30f,.46f,.19f}},
        {"Cornflower",.80,.30,85,245,30,false,false,{.29f,.46f,.27f}},
        {"Fireweed",1.65,.23,180,2190,90,false,false,{.21f,.44f,.18f}},
        {"Buttercup",.55,.40,90,1460,60,false,false,{.28f,.48f,.14f}},
        {"Willow",18,.42,4200,27000,550,false,true,{.32f,.46f,.20f}},
        {"Alder",21,.30,4800,36500,650,false,true,{.18f,.37f,.16f}},
        {"Common reed",2.8,.22,140,1825,120,true,false,{.46f,.53f,.24f}},
        {"Cattail",2.1,.25,120,1460,100,true,false,{.30f,.46f,.18f}},
        {"Marsh sedge",.95,.65,100,2200,75,false,false,{.43f,.49f,.17f}},
        {"Sphagnum moss",.13,4.0,110,2200,120,false,false,{.37f,.52f,.16f}},
        {"Water lily",.18,3.1,180,3650,60,true,false,{.19f,.41f,.19f}},
        {"Pondweed",1.7,.32,100,1460,60,true,false,{.24f,.44f,.18f}},
        {"Marram grass",1.05,.45,170,2600,80,false,false,{.65f,.62f,.34f}}
    }};
    const auto i=static_cast<unsigned>(s);
    if(i>=traits.size()) throw std::invalid_argument("Unknown plant species");
    return traits[i];
}
bool freshwaterPlant(Species s) {return s==Species::Reed || s==Species::Cattail || s==Species::WaterLily || s==Species::Pondweed;}
std::string_view stageName(GrowthStage s) {
    constexpr std::array<std::string_view,6> names{"seed","seedling","juvenile","mature","senescent","dead"};
    const auto i=static_cast<unsigned>(s); return i<names.size() ? names[i] : "unknown";
}
double plantSuitability(Species species,const Sample& s,double slope,double rock,double sea) {
    const auto& t=plantTraits(species);
    if(species==Species::None || !std::isfinite(s.temperature) || !std::isfinite(s.precipitation) ||
       !std::isfinite(s.elevation) || !std::isfinite(slope) || !std::isfinite(rock) || !std::isfinite(sea)) return 0;
    if(species>=Species::Willow) {
        if(s.ocean || s.temperature<274 || s.temperature>307 || slope>.5) return 0;
        const double wet=s.wetness,depth=s.freshwater ? s.waterDepth : 0;
        if(species==Species::Marram) return !s.freshwater && s.sand>.4 && s.elevation>sea+.6 ? clamp(s.sand*(1-slope),0.,1.) : 0;
        if(wet<.35) return 0;
        if(species==Species::Willow || species==Species::Alder) return depth<.35 ? wet*.85 : 0;
        if(species==Species::Sphagnum) return depth<.06 && s.temperature<298 ? wet : 0;
        if(species==Species::Sedge) return depth<.35 ? wet : 0;
        if(species==Species::Reed) return depth<1.6 ? wet : 0;
        if(species==Species::Cattail) return depth>.03 && depth<.85 ? wet : 0;
        if(species==Species::WaterLily) return depth>.25 && depth<2.8 && s.temperature>280 ? std::exp(-.3*depth) : 0;
        if(species==Species::Pondweed) return depth>.25 && depth<5 ? std::exp(-.45*depth) : 0;
        return 0;
    }
    if(t.aquatic) {
        const double depth=sea-s.elevation;
        if(!s.ocean || depth<.5 || depth>30 || slope>.8) return 0;
        if(species==Species::Seagrass && (s.temperature<277 || s.temperature>303 || slope>.35 || rock>.62 || depth>22)) return 0;
        if(species==Species::Kelp && (s.temperature<276 || s.temperature>294 || rock<.38 || depth<2)) return 0;
        if(species==Species::Rockweed && (s.temperature<276 || s.temperature>300 || rock<.32 || depth>10)) return 0;
        // Beer-Lambert light attenuation with a prescribed clear-water coefficient.
        return clamp(std::exp(-.12*depth)/.6,.05,1.0);
    }
    if(s.ocean || s.freshwater || s.wetness>.55 || s.elevation<=sea+2 || slope>=.65 || s.temperature<=267 || s.temperature>=315 || s.precipitation<=180) return 0;
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
        case Species::Daisy: low=278;high=300;rain=400;break;
        case Species::Clover: low=280;high=301;rain=550;break;
        case Species::Poppy: low=280;high=304;rain=250;break;
        case Species::Cornflower: low=278;high=300;rain=300;break;
        case Species::Fireweed: low=273;high=294;rain=500;break;
        case Species::Buttercup: low=277;high=298;rain=700;break;
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
const FlowerTraits& flowerTraits(Species species) {
    static const std::array<FlowerTraits,speciesCount> traits=[] {
        std::array<FlowerTraits,speciesCount> t{};
        const auto set=[&](Species s,FlowerForm form,Color color,unsigned petals,double radius,double heat,double duration,double age) {
            t[static_cast<unsigned>(s)]={form,color,petals,radius,5,heat,duration,age};
        };
        set(Species::Oak,FlowerForm::Catkin,{.66f,.69f,.25f},0,.11,110,110,4000);
        set(Species::Birch,FlowerForm::Catkin,{.71f,.64f,.28f},0,.10,90,90,1500);
        set(Species::Acacia,FlowerForm::Ball,{.98f,.83f,.24f},0,.07,230,270,900);
        set(Species::TropicalTree,FlowerForm::Star,{.98f,.78f,.81f},5,.09,350,520,1800);
        set(Species::Heather,FlowerForm::Bell,{.84f,.32f,.68f},4,.024,950,500,90);
        set(Species::MeadowGrass,FlowerForm::Spike,{.72f,.66f,.36f},0,.045,300,280,45);
        set(Species::Seagrass,FlowerForm::Spike,{.59f,.73f,.35f},0,.025,200,240,120);
        set(Species::Willow,FlowerForm::Catkin,{.78f,.77f,.42f},0,.11,80,100,1600);
        set(Species::Alder,FlowerForm::Catkin,{.62f,.49f,.22f},0,.10,55,90,1900);
        set(Species::Reed,FlowerForm::Spike,{.71f,.58f,.34f},0,.065,1000,650,100);
        set(Species::Cattail,FlowerForm::Spike,{.39f,.23f,.10f},0,.07,750,650,90);
        set(Species::Sedge,FlowerForm::Spike,{.53f,.47f,.21f},0,.04,300,500,70);
        set(Species::WaterLily,FlowerForm::Cup,{.98f,.91f,.91f},12,.14,450,800,130);
        set(Species::Pondweed,FlowerForm::Spike,{.62f,.60f,.31f},0,.025,500,500,80);
        set(Species::Marram,FlowerForm::Spike,{.81f,.75f,.48f},0,.045,550,500,110);
        set(Species::Daisy,FlowerForm::Daisy,{.98f,.97f,.85f},12,.045,300,500,75);
        set(Species::Clover,FlowerForm::Ball,{.87f,.30f,.58f},0,.035,240,650,65);
        set(Species::Poppy,FlowerForm::Cup,{.94f,.12f,.09f},4,.060,230,330,50);
        set(Species::Cornflower,FlowerForm::Star,{.25f,.43f,.98f},9,.037,380,400,60);
        set(Species::Fireweed,FlowerForm::Spike,{.94f,.31f,.66f},4,.032,800,500,150);
        set(Species::Buttercup,FlowerForm::Cup,{.99f,.83f,.10f},5,.025,180,430,65);
        return t;
    }();
    const auto i=static_cast<unsigned>(species);if(i>=traits.size()) throw std::invalid_argument("Unknown flowering species");
    return traits[i];
}
std::string_view flowerPhaseName(FlowerPhase phase) {
    constexpr std::array<std::string_view,4> names{"vegetative","buds","flowering","seed heads"};
    const auto i=static_cast<unsigned>(phase);return i<names.size() ? names[i] : "unknown";
}
double growingDegreeDays(double days,double year,double mean,double amplitude,double base) {
    if(!std::isfinite(days) || !std::isfinite(year) || year<=0 || days<0 || days>year ||
       !std::isfinite(mean) || !std::isfinite(amplitude) || amplitude<0 || !std::isfinite(base))
        throw std::invalid_argument("Invalid thermal-time parameters");
    const double excess=mean-base,omega=tau/year;
    if(amplitude<1e-12) return std::max(0.,excess)*days;
    if(excess<=-amplitude) return 0;
    const double start=excess>=amplitude ? 0 : std::acos(excess/amplitude)/omega;
    const double end=clamp(days,start,year-start);
    return std::max(0.,excess*(end-start)-amplitude/omega*(std::sin(omega*end)-std::sin(omega*start)));
}
FlowerCycle flowerCycle(Species species,std::uint64_t id,const Config& config,Vec3 up,const Sample& annual) {
    const auto& f=flowerTraits(species);FlowerCycle c;c.yearDays=config.year/86400;
    if(f.form==FlowerForm::None) return c;
    const double lat=latitude(up),mean=annual.temperature-273.15;
    const double amplitude=(2+18*std::abs(std::sin(lat)))*std::abs(std::sin(config.obliquity))/std::sin(radians(23.44))*(plantTraits(species).aquatic ? .35 : 1.);
    // Invert the orbit at the local summer solstice, then apply a thermal lag.
    const double anomaly=(lat>=0 ? pi/2 : pi*1.5)+radians(77.1);
    const double eccentric=std::atan2(std::sqrt(1-config.eccentricity*config.eccentricity)*std::sin(anomaly),config.eccentricity+std::cos(anomaly));
    const double summer=(eccentric-config.eccentricity*std::sin(eccentric))/tau*c.yearDays+c.yearDays*20/365.25;
    c.winterDay=summer+c.yearDays*.5;
    // Weakly seasonal habitats have individually staggered annual flowering.
    c.winterDay+=(1-smoothstep(3,7,amplitude))*randomUnit(id+909)*c.yearDays;
    const double start=f.heatStart*(.92+.16*randomUnit(id+335)),duration=f.heatDuration;
    const double total=growingDegreeDays(c.yearDays,c.yearDays,mean,amplitude,f.baseC);
    if(total<start+duration*.25) return c;
    const auto when=[&](double heat) {
        double low=0,high=c.yearDays;
        for(int i=0;i<28;++i) {const double mid=(low+high)/2;if(growingDegreeDays(mid,c.yearDays,mean,amplitude,f.baseC)<heat) low=mid;else high=mid;}
        return (low+high)/2;
    };
    c.budDay=when(start-duration*.15);c.openDay=when(start);
    c.closeDay=std::min(c.yearDays*.92,when(start+duration));
    c.endDay=std::min(c.yearDays*.99,c.closeDay+c.yearDays*.18);
    c.enabled=c.closeDay>c.openDay+1 && c.endDay>c.closeDay;return c;
}
FlowerState flowerState(Species species,const PlantState& plant,const FlowerCycle& c,double day) {
    if(!std::isfinite(day)) throw std::invalid_argument("Invalid flowering calendar");
    const auto& f=flowerTraits(species);FlowerState state;
    if(!c.enabled || f.form==FlowerForm::None || plant.foliage<=0 ||
       (plant.stage!=GrowthStage::Mature && plant.stage!=GrowthStage::Senescent)) return state;
    const double raw=std::fmod(day-c.winterDay,c.yearDays),phase=raw<0 ? raw+c.yearDays : raw;
    // A young plant which missed this year's reproductive window cannot acquire
    // seed heads retroactively. The local seed bank is still an approximation.
    const double ageAtOpening=plant.ageDays-(phase-c.openDay);
    if(ageAtOpening<f.reproductiveDays || plant.ageDays<f.reproductiveDays*.85) return state;
    const double span=c.closeDay-c.openDay;
    state.buds=smoothstep(c.budDay,c.openDay,phase)*(1-smoothstep(c.openDay,c.openDay+span*.18,phase))*plant.foliage;
    state.bloom=smoothstep(c.openDay,c.openDay+span*.15,phase)*(1-smoothstep(c.closeDay-span*.25,c.closeDay,phase))*plant.foliage;
    state.seeds=smoothstep(c.closeDay-span*.20,c.closeDay+span*.20,phase)*(1-smoothstep(c.endDay-span*.20,c.endDay,phase))*plant.foliage;
    state.phase=state.bloom>.02 ? FlowerPhase::Flowering : state.buds>.02 ? FlowerPhase::Budding : state.seeds>.02 ? FlowerPhase::Fruiting : FlowerPhase::Dormant;
    return state;
}
void Flora::setCalendarDay(double day) {
    if(!std::isfinite(day) || std::abs(day)>1e7) throw std::invalid_argument("Biological calendar is outside the supported range");
    epochDay_=day-days_;
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

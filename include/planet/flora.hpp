#pragma once
#include "planet/model.hpp"

namespace planet {
// Ecological archetypes, not a calibrated model of individual botanical species.
enum class Species : unsigned { None, Oak, Birch, Pine, Acacia, TropicalTree, Juniper, Heather, MeadowGrass, Fern, Seagrass, Kelp, Rockweed, Daisy, Clover, Poppy, Cornflower, Fireweed, Buttercup, Willow, Alder, Reed, Cattail, Sedge, Sphagnum, WaterLily, Pondweed, Marram, Spruce, Fir, Beech, Redwood, Kapok, StranglerFig, Palm, TreeFern, Bamboo, Monstera, Bromeliad, ForestMoss, Count };
constexpr auto speciesCount=static_cast<unsigned>(Species::Count);
enum class GrowthStage { Seed, Seedling, Juvenile, Mature, Senescent, Dead };
struct PlantTraits {
    std::string_view name;
    double height, widthRatio, maturityDays, lifespanDays, decayDays;
    bool aquatic, woody;
    Color leaves;
};
const PlantTraits& plantTraits(Species species);
bool freshwaterPlant(Species species);
bool treeSpecies(Species species);
bool coniferSpecies(Species species);
bool tropicalCanopy(Species species);
std::string_view stageName(GrowthStage stage);
// Relative growth potential, under a fixed annual climate and substrate.
double plantSuitability(Species species,const Sample& habitat,double slope,double rockiness,double seaLevel);
struct PlantState {
    double ageDays=0, size=0, foliage=0;
    GrowthStage stage=GrowthStage::Seed;
    std::uint64_t generation=0;
};
PlantState plantState(Species species,std::uint64_t siteId,double days,double suitability,bool young=false);
enum class FlowerForm { None, Daisy, Ball, Cup, Star, Spike, Bell, Catkin };
enum class FlowerPhase { Dormant, Budding, Flowering, Fruiting };
struct FlowerTraits {
    FlowerForm form=FlowerForm::None;
    Color petals{};
    unsigned petalsCount=0;
    double radius=0,baseC=5,heatStart=0,heatDuration=0,reproductiveDays=0;
};
const FlowerTraits& flowerTraits(Species species);
std::string_view flowerPhaseName(FlowerPhase phase);
// A prescribed annual temperature curve, integrated above a species' base
// temperature. This is reference-climate phenology, not a weather history.
struct FlowerCycle {
    bool enabled=false;
    double yearDays=365.25,winterDay=0,budDay=0,openDay=0,closeDay=0,endDay=0;
};
struct FlowerState {
    double buds=0,bloom=0,seeds=0;
    FlowerPhase phase=FlowerPhase::Dormant;
};
double growingDegreeDays(double elapsedDays,double yearDays,double meanC,double amplitude,double baseC);
FlowerCycle flowerCycle(Species species,std::uint64_t siteId,const Config& config,Vec3 up,const Sample& annual);
FlowerState flowerState(Species species,const PlantState& plant,const FlowerCycle& cycle,double calendarDays);
class Flora {
public:
    void advance(const World& world,double modelSeconds);
    void advanceDays(const World& world,double days);
    void setRate(double multiplier);
    void setYoung(bool young) { young_=young; }
    double days() const { return days_; }
    double rate() const { return rate_; }
    bool young() const { return young_; }
    void setCalendarDay(double day);
    double calendarDay() const { return epochDay_+days_; }
private:
    double days_=0,rate_=1,epochDay_=79;
    bool young_=false;
};
} // namespace planet

#pragma once
#include "planet/model.hpp"

namespace planet {
// Ecological archetypes, not a calibrated model of individual botanical species.
enum class Species : unsigned { None, Oak, Birch, Pine, Acacia, TropicalTree, Juniper, Heather, MeadowGrass, Fern, Seagrass, Kelp, Rockweed, Count };
constexpr auto speciesCount=static_cast<unsigned>(Species::Count);
enum class GrowthStage { Seed, Seedling, Juvenile, Mature, Senescent, Dead };
struct PlantTraits {
    std::string_view name;
    double height, widthRatio, maturityDays, lifespanDays, decayDays;
    bool aquatic, woody;
    Color leaves;
};
const PlantTraits& plantTraits(Species species);
std::string_view stageName(GrowthStage stage);
// Relative growth potential, under a fixed annual climate and substrate.
double plantSuitability(Species species,const Sample& habitat,double slope,double rockiness,double seaLevel);
struct PlantState {
    double ageDays=0, size=0, foliage=0;
    GrowthStage stage=GrowthStage::Seed;
    std::uint64_t generation=0;
};
PlantState plantState(Species species,std::uint64_t siteId,double days,double suitability,bool young=false);
class Flora {
public:
    void advance(const World& world,double modelSeconds);
    void advanceDays(const World& world,double days);
    void setRate(double multiplier);
    void setYoung(bool young) { young_=young; }
    double days() const { return days_; }
    double rate() const { return rate_; }
    bool young() const { return young_; }
private:
    double days_=0,rate_=1;
    bool young_=false;
};
} // namespace planet

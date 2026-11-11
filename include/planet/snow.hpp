#pragma once
#include "planet/weather.hpp"
#include <map>
#include <deque>

namespace planet {
enum class SnowExperiment { Natural, Fine, Large, Blizzard, Drifting, Thaw };
std::string_view snowExperimentName(SnowExperiment value);
std::optional<SnowExperiment> snowExperiment(std::string_view value);
double snowFraction(double temperatureK,double relativeHumidity);
double referenceSnowDepth(const Sample& climate);
struct SnowForcing {
    double temperature=268.15,humidity=.8,precipitation=0; // K, 0..1, m water equivalent / s
    Wind2 wind{}; // estimated 10 m wind
};
struct SnowCell {
    double bed=0,swe=0,depth=0,airborne=0,vegetation=0,meltRate=0;
    bool solid=true;
    double density() const {return depth>1e-12 ? swe*1000/depth : 100;}
};
struct SnowExchange {double snowfallM3=0,meltM3=0,sublimatedM3=0,exportM3=0,redistributedM3=0;};
// Conservative surface / suspended snow reservoirs, with open outflow edges.
// SWE is liquid-water-equivalent depth, not geometrical snow depth.
class SnowGrid {
public:
    SnowGrid(int size,double spacing);
    SnowExchange advance(double seconds,const SnowForcing& forcing);
    double waterVolume() const;
    int size() const {return size_;}
    double spacing() const {return spacing_;}
    std::vector<SnowCell> cells;
private:
    int size_;double spacing_;
};
struct SnowBudget {
    double initialM3=0,storedM3=0,snowfallM3=0,meltM3=0,sublimatedM3=0,exportM3=0,domainM3=0,redistributedM3=0;
    double residual() const {return storedM3+meltM3+sublimatedM3+exportM3-initialM3-snowfallM3-domainM3;}
};
struct SnowSample {bool available=false;double bed=0,depth=0,swe=0,airborne=0,meltRate=0,weight=0;};
struct SnowAtmosphere {SnowForcing forcing;double fraction=0,large=0,fallingMmH=0,blowing=0,visibility=100000;};
struct SnowParticle {Vec3 up;double altitude=0,radius=0,opacity=0;bool coarse=false,blowing=false;};
class Snow {
    friend class StateCodec;
public:
    static constexpr int resolution=32;
    static constexpr double spacing=8,extent=resolution*spacing;
    Snow(const World& world,Vec3 observer,double solarSeconds=79*86400);
    void synchronize(const World& world,Vec3 observer);
    void step(const World& world,const Weather& weather,double seconds);
    void animate(const World& world,const Weather& weather,Vec3 observer,double seconds);
    void setClock(double seconds);
    void setExperiment(SnowExperiment value) {experiment_=value;}
    SnowExperiment experiment() const {return experiment_;}
    void setObserved(bool value) {observed_=value;}
    bool enabled() const {return enabled_;}
    bool active() const {return enabled_ && observed_;}
    SnowSample sample(Vec3 unit) const;
    SnowSample sampleLocal(double x,double z) const;
    double depth(Vec3 unit,const Sample& reference) const;
    double walkingDepth(Vec3 unit) const;
    Vec3 directionAt(double x,double z) const;
    SnowAtmosphere atmosphere(const World& world,const Weather& weather,Vec3 unit,double elevation) const;
    std::vector<SnowParticle> particles(const World& world,const Weather& weather,Vec3 observer,double altitude) const;
    SnowBudget budget() const;
    const SnowGrid& grid() const {return grid_;}
    std::uint64_t revision() const {return revision_;}
    std::uint64_t steps() const {return steps_;}
    double seconds() const {return time_;}
    double solarSeconds() const {return solar_;}
    double liquidRate() const {return liquidRate_;}
    double solidFraction() const {return fraction_;}
    double cellX(int x) const {return (originX_+x)*spacing;}
    double cellZ(int z) const {return (originZ_+z)*spacing;}
private:
    SnowCell makeCell(const World& world,int x,int z) const;
    void move(const World& world,Vec3 observer,int x,int z,bool reset);
    Config config_;
    Vec3 anchor_,east_,north_;
    int originX_=-resolution/2,originZ_=-resolution/2;
    SnowGrid grid_{resolution,spacing};
    using Key=std::pair<int,int>;
    std::map<Key,SnowCell> cache_;
    std::deque<Key> cacheOrder_;
    std::vector<double> displayDepth_;
    SnowBudget totals_;
    SnowExperiment experiment_=SnowExperiment::Natural;
    double solar_=0,time_=0,driftX_=0,driftZ_=0,liquidRate_=0,fraction_=0;
    std::uint64_t revision_=0,steps_=0;
    unsigned geometry_=0;
    bool enabled_=false,observed_=true,initialized_=false;
};
Vec3 findSnowfield(const World& world,Vec3 near,double solarSeconds);
} // namespace planet

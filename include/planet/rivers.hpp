#pragma once
#include "planet/model.hpp"
#include <vector>
#include <unordered_map>

namespace planet {
struct RiverSection { double width{},depth{},speed{}; };
RiverSection riverSection(double discharge,double slope,double roughness=.035);
enum class RiverKind { Stream, River, Major };
std::optional<RiverKind> riverKind(std::string_view name);
struct DrainageNode {
    Vec3 up{};
    double elevation{},filled{},area{},runoff{},discharge{},distance{},level{};
    int downstream=-1;
    bool ocean=false;
};
struct RiverPoint {
    Vec3 up{},left{};
    double level{},discharge{},width{},depth{},along{};
};
struct RiverReach { std::size_t node{};std::vector<RiverPoint> points; };
struct RiverSample {
    bool channel=false,wet=false;
    Vec3 center{},downstream{};
    double distance{},lateral{},level{},bed{},depth{},width{},discharge{},speed{},along{},slope{};
};
struct RiverSite { Vec3 bank{},center{},downstream{};double width{},discharge{}; };
// A reference-climate drainage atlas, not a time-dependent continental flood solver.
class RiverNetwork {
public:
    static constexpr int columns=128,rows=64;
    explicit RiverNetwork(const World& world);
    RiverSample sample(Vec3 up,double naturalHeight=std::numeric_limits<double>::quiet_NaN()) const;
    Vec3 oceanCurrent(Vec3 up,double belowSurface=0) const;
    RiverSite find(RiverKind kind,Vec3 near) const;
    const std::vector<DrainageNode>& nodes() const { return nodes_; }
    const std::vector<RiverReach>& reaches() const { return reaches_; }
    double runoff() const { return totalRunoff_; }
    double oceanDischarge() const { return oceanDischarge_; }
    double circulationResidual() const { return circulationResidual_; }
    std::vector<std::pair<std::size_t,std::size_t>> nearby(Vec3 up,double radius) const;
    RiverPoint point(std::size_t reach,std::size_t segment,double t) const;
private:
    struct Segment { std::size_t reach{},point{}; };
    static constexpr int binsX=2048,binsY=1024;
    std::array<int,2> bin(Vec3 p) const;
    std::vector<std::size_t> candidates(Vec3 up,double radius) const;
    void buildCirculation();
    World base_;
    std::vector<DrainageNode> nodes_;
    std::vector<RiverReach> reaches_;
    std::vector<Segment> segments_;
    std::unordered_map<int,std::vector<std::size_t>> buckets_;
    std::vector<double> streamfunction_;
    double totalRunoff_=0,oceanDischarge_=0,circulationResidual_=0;
    mutable Vec3 cachedUp_{};
    mutable RiverSample cachedSample_;
    mutable double cachedNatural_=0;
    mutable bool sampleCached_=false;
};
} // namespace planet

#pragma once
#include "planet/math.hpp"
#include <array>
#include <optional>
#include <string_view>

namespace planet {
enum class Landform { Lowland, Mountains, Plateau, Canyon, Gorge, Karst, Wetland, Pond, Lake, Bay, Cliff, Spit, Grotto };
std::string_view landformName(Landform type);
std::optional<Landform> landmarkType(std::string_view name);
struct Relief {
    double elevation{}, incision{}, ruggedness{}; Landform type=Landform::Lowland;
    double waterLevel=0,wetness=0,sand=0;
    bool inland=false;
};
Relief shapeLandforms(Vec3 unit,double radius,double seaLevel,std::uint64_t seed,double height,double mountainWeight);
struct CaveColumn {
    double floor{},ceiling{},field=-1; // elevations ASL; positive field inside the horizontal footprint
    bool valid() const { return field>0 && ceiling>floor; }
};
struct CaveNode { double x,z,y,width,height; }; // local metres; y relative to the site's base elevation
struct CaveSite {
    std::uint64_t id{};
    Vec3 anchor{},forward{},right{};
    double radius{},base{};
    bool coastal=false;
    std::array<CaveNode,10> nodes{};
    std::array<std::array<unsigned,2>,9> links{};
    Vec3 directionAt(double x,double z) const;
    Vec3 local(Vec3 unit) const; // x/right and z/forward
    double cover(Vec3 unit,double originalHeight) const;
    CaveColumn column(Vec3 unit) const;
    CaveColumn columnLocal(double x,double z) const;
    bool protectedGround(Vec3 unit) const;
};
struct CaveAddress { std::uint64_t id{}; Vec3 anchor{}; bool eligible=false; };
CaveAddress caveAddress(Vec3 unit,double radius,std::uint64_t seed,int offsetX=0,int offsetY=0);
CaveSite makeCave(CaveAddress address,double radius,double base);
struct Landmark {
    Vec3 position{},lookToward{};
    Landform type=Landform::Lowland;
};
} // namespace planet

#include "planet/state.hpp"
#include "planet/file_io.hpp"
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace planet {
namespace {
// Increment the format version in both readers and writer when changing field order, types or module count.
constexpr std::size_t maximumFile=64*1024*1024,maximumAllocation=128*1024*1024,headerSize=28;
constexpr std::array<unsigned char,8> magic{'P','L','S','T','A','T','E',0};
void check(bool value,const char* message) {if(!value) throw std::invalid_argument(message);}
std::uint64_t checksum(std::span<const std::byte> bytes) {
    std::uint64_t hash=14695981039346656037ULL;
    for(const auto b:bytes) {hash^=std::to_integer<unsigned char>(b);hash*=1099511628211ULL;}
    return hash;
}
World configured(Config c,Modules m) {World world(c);world.modules()=m;return world;}
void unit(Vec3 p) {check(std::abs(length(p)-1)<1e-6,"Invalid saved coordinate frame");}
std::filesystem::path backup(const std::filesystem::path& path) {auto p=path;p+=".bak";return p;}
}
LoadedState::LoadedState(Config c,Modules m,Vec3 p,double seconds):world(configured(c,m)),explorer(world),simulation(world,p,seconds) {}
class StateCodec {
public:
    class Archive {
    public:
        bool reading=false;std::span<const std::byte> input;std::vector<std::byte> output;
        std::size_t cursor=0,allocated=0;
        Archive()=default;
        explicit Archive(std::span<const std::byte> bytes):reading(true),input(bytes) {}
        template<class... T> void operator()(T&... values) {(value(values),...);}
        void integer(std::uint64_t& bits,unsigned bytes) {
            if(reading) {check(bytes<=input.size()-cursor,"Truncated saved state");bits=0;for(unsigned i=0;i<bytes;++i) bits|=std::uint64_t(std::to_integer<unsigned char>(input[cursor++]))<<(8*i);}
            else {check(output.size()+bytes<=maximumFile,"Saved state exceeds size limit");for(unsigned i=0;i<bytes;++i) output.push_back(static_cast<std::byte>((bits>>(8*i))&255));}
        }
        template<class T> void value(T& v) {
            if constexpr(std::is_same_v<T,bool>) {std::uint64_t n=v ? 1 : 0;integer(n,1);check(n<2,"Invalid saved boolean");if(reading)v=n!=0;}
            else if constexpr(std::is_floating_point_v<T>) {
                using U=std::conditional_t<sizeof(T)==8,std::uint64_t,std::uint32_t>;
                std::uint64_t n=std::bit_cast<U>(v);integer(n,sizeof(T));if(reading)v=std::bit_cast<T>(static_cast<U>(n));check(std::isfinite(v),"Nonfinite saved number");
            } else if constexpr(std::is_integral_v<T>) {
                using U=std::make_unsigned_t<T>;std::uint64_t n=static_cast<U>(v);integer(n,sizeof(T));if(reading) v=std::bit_cast<T>(static_cast<U>(n));
            } else if constexpr(std::is_enum_v<T>) {
                std::uint32_t n=static_cast<std::uint32_t>(v);value(n);
                constexpr unsigned limit=[] {if constexpr(std::is_same_v<T,Species>)return speciesCount;else if constexpr(std::is_same_v<T,Phenomenon>)return 8U;else if constexpr(std::is_same_v<T,DeerActivity>)return 5U;else if constexpr(std::is_same_v<T,StormKind>)return 3U;else if constexpr(std::is_same_v<T,DetailKind> || std::is_same_v<T,FlowerPhase>)return 4U;else return 6U;}();
                check(n<limit,"Unknown saved enum value");if(reading)v=static_cast<T>(n);
            } else if constexpr(requires {v.has_value();v.emplace();}) {
                bool present=v.has_value();value(present);if(reading) {if(present)v.emplace();else v.reset();}if(present)value(*v);
            } else if constexpr(requires {typename T::mapped_type;}) {
                std::uint32_t count=static_cast<std::uint32_t>(v.size());value(count);allocation(count,sizeof(typename T::value_type));
                if(reading) {v.clear();for(std::uint32_t i=0;i<count;++i) {typename T::key_type key{};typename T::mapped_type item{};(*this)(key,item);check(v.emplace(key,std::move(item)).second,"Duplicate saved cache address");}}
                else for(auto& entry:v) {auto key=entry.first;(*this)(key,entry.second);}
            } else if constexpr(requires {v.resize(0);typename T::value_type;}) {
                std::uint32_t count=static_cast<std::uint32_t>(v.size());value(count);allocation(count,sizeof(typename T::value_type));if(reading)v.resize(count);for(auto& item:v)value(item);
            } else if constexpr(requires {v.first;v.second;}) {(*this)(v.first,v.second);}
            else if constexpr(requires {std::tuple_size<T>::value;}) {for(auto& item:v)value(item);}
            else StateCodec::fields(*this,v);
        }
        void allocation(std::size_t count,std::size_t size) {
            check(count<=100000 && count<=maximumAllocation/size,"Saved container too large");
            if(reading) {check(count<=input.size()-cursor,"Impossible saved container size");allocated+=count*size;check(allocated<=maximumAllocation,"Saved allocation budget exceeded");}
        }
    };
    static void fields(Archive& a,Vec3& v) {a(v.x,v.y,v.z);}
    static void fields(Archive& a,Color& v) {a(v.r,v.g,v.b);}
    static void fields(Archive& a,Wind2& v) {a(v.east,v.north);}
    static void fields(Archive& a,Config& v) {a(v.seed,v.radius,v.gravity,v.solarConstant,v.obliquity,v.eccentricity,v.day,v.year,v.seaLevel,v.emissivity,v.albedo);}
    static void fields(Archive& a,Modules& v) {for(unsigned i=0;i<moduleCount;++i) {auto m=static_cast<Module>(i);bool on=v.requested(m);a(on);if(a.reading)v.set(m,on);}}
    static void fields(Archive& a,SaveMetadata& v) {a(v.savedAt,v.seed,v.latitude,v.longitude,v.seconds);check(v.savedAt<=32503680000ULL,"Invalid saved timestamp");check(std::abs(v.latitude)<=90 && std::abs(v.longitude)<=180 && std::abs(v.seconds)<=1e12,"Invalid saved location or time");}
    static void fields(Archive& a,SessionState& v) {
        a(v.seconds,v.timeScale,v.phenomenonEpoch,v.paused,v.globe,v.help,v.headlamp,v.windTracers,v.waterTracers,v.floraInfo,v.faunaInfo,v.weatherLayer,v.phenomenon,v.globeOutward,v.globeUp);
        check(std::abs(v.seconds)<=1e12 && std::abs(v.phenomenonEpoch)<=1e12 && v.timeScale>=1 && v.timeScale<=86400 && v.weatherLayer<6,"Invalid saved session");
        unit(v.globeOutward);unit(v.globeUp);check(std::abs(dot(v.globeOutward,v.globeUp))<1e-6,"Invalid saved globe camera");
    }
    static void fields(Archive& a,PlantState& v) {a(v.ageDays,v.size,v.foliage,v.stage,v.generation);}
    static void fields(Archive& a,FlowerCycle& v) {a(v.enabled,v.yearDays,v.winterDay,v.budDay,v.openDay,v.closeDay,v.endDay);}
    static void fields(Archive& a,FlowerState& v) {a(v.buds,v.bloom,v.seeds,v.phase);}
    static void fields(Archive& a,SurfaceObject& v) {a(v.id,v.kind,v.up,v.elevation,v.height,v.radius,v.angle,v.evergreen,v.species,v.suitability,v.plant,v.flowerCycle,v.flowers,v.waterDepth);}
    static void fields(Archive& a,CaveNode& v) {a(v.x,v.z,v.y,v.width,v.height);}
    static void fields(Archive& a,CaveSite& v) {a(v.id,v.anchor,v.forward,v.right,v.radius,v.base,v.coastal,v.nodes,v.links);for(auto link:v.links)check(link[0]<v.nodes.size() && link[1]<v.nodes.size(),"Invalid saved cave graph");}
    static void fields(Archive& a,Explorer& v) {
        a(v.flying,v.flightSpeed,v.obstacles_,v.obstacleAnchor_,v.obstacleSeed_,v.obstacleModules_,v.obstacleFloraDay_,v.obstacleYoung_,v.cave_,v.up_,v.heading_,v.swimVelocity_,v.altitude_,v.pitch_,v.verticalSpeed_,v.grounded_,v.jumpHeld_,v.underground_,v.swimming_,v.underwater_);
        unit(v.up_);unit(v.heading_);check(std::abs(dot(v.up_,v.heading_))<1e-6 && std::abs(v.pitch_)<=pi/2 && v.flightSpeed>=1 && v.flightSpeed<=20000 && v.altitude_>=-100000 && v.altitude_<=1e9,"Invalid saved explorer");
    }
    static void fields(Archive& a,WeatherCell& v) {a(v.vapour,v.cloud,v.temperature,v.wind,v.uplift,v.rainRate);check(v.vapour>=0 && v.cloud>=0 && v.temperature>0,"Invalid saved weather cell");}
    static void fields(Archive& a,WeatherParticle& v) {a(v.x,v.z,v.height,v.size,v.id);}
    static void fields(Archive& a,Weather::Background& v) {a(v.temperature,v.pressure,v.airMass,v.terrain,v.wetness,v.pressureA,v.pressureB,v.forcingWeight);}
    static void fields(Archive& a,Weather& v) {
        a(v.config_,v.anchor_,v.east_,v.north_,v.cells_,v.background_,v.clouds_,v.tracers_,v.layerAltitude_,v.time_,v.pending_,v.annualTemperature_,v.initialWater_,v.evaporation_,v.precipitation_,v.domainExchange_,v.synopticExchange_,v.steps_,v.backgroundModules_,v.windEnabled_,v.cloudsEnabled_);
        check(v.cells_.size()==Weather::resolution*Weather::resolution && v.background_.size()==v.cells_.size() && v.pending_>=0 && v.pending_<Weather::fixedStep+1e-6,"Invalid saved weather grid");unit(v.anchor_);unit(v.east_);unit(v.north_);
    }
    static void fields(Archive& a,GlobalColumn& v) {a(v.vapour,v.cloud,v.temperature,v.rainRate);check(v.vapour>=0 && v.cloud>=0 && v.temperature>0,"Invalid saved global column");}
    static void fields(Archive& a,Storm& v) {a(v.id,v.generation,v.kind,v.center,v.age,v.lifetime,v.radius,v.deficit);unit(v.center);check(v.radius>0 && v.lifetime>0,"Invalid saved storm");}
    static void fields(Archive& a,GlobalWeather::Habitat& v) {a(v.direction,v.temperature,v.pressure,v.airMass,v.wetness,v.area,v.ocean);}
    static void fields(Archive& a,GlobalWeather& v) {
        a(v.config_,v.habitat_,v.cells_,v.storms_,v.elapsed_,v.pending_,v.effects_,v.solarTime_,v.initialWater_,v.evaporation_,v.precipitation_,v.steps_,v.geometry_,v.enabled_,v.cloudsEnabled_);
        check(v.cells_.size()==GlobalWeather::columns*GlobalWeather::rows && v.habitat_.size()==v.cells_.size() && v.pending_>=0 && v.pending_<GlobalWeather::fixedStep+1e-6 && v.storms_.size()<=64,"Invalid saved global weather");
    }
    static void fields(Archive& a,GroundCell& v) {a(v.rock,v.soil,v.initialSoil,v.water,v.soilWater,v.sediment,v.roughness,v.conductivity,v.vegetation,v.sea,v.reservoir,v.reservoirLevel,v.masked);check(v.water>=0 && v.soil>=0 && v.soilWater>=0 && v.sediment>=0,"Invalid saved soil reservoir");}
    static void fields(Archive& a,RunoffBudget& v) {a(v.initialWaterM3,v.storedWaterM3,v.rainM3,v.meltM3,v.evaporatedM3,v.drainedM3,v.boundaryM3,v.domainWaterM3,v.initialSolidKg,v.storedSolidKg,v.sedimentOutKg,v.domainSolidKg,v.erodedKg,v.depositedKg);}
    static void fields(Archive& a,RunoffGrid& v) {a(v.cells,v.eastFlow,v.northFlow);check(v.cells.size()==4096 && v.eastFlow.size()==4160 && v.northFlow.size()==4160,"Invalid saved runoff dimensions");}
    static void fields(Archive& a,SnowCell& v) {a(v.bed,v.swe,v.depth,v.airborne,v.vegetation,v.meltRate,v.solid);check(v.swe>=0 && v.depth>=0 && v.airborne>=0,"Invalid saved snow reservoir");}
    static void fields(Archive& a,SnowBudget& v) {a(v.initialM3,v.storedM3,v.snowfallM3,v.meltM3,v.sublimatedM3,v.exportM3,v.domainM3,v.redistributedM3);}
    static void fields(Archive& a,SnowGrid& v) {a(v.cells);check(v.cells.size()==1024,"Invalid saved snow dimensions");}
    template<class Map> static void cacheBounds(const Map& cache) {for(const auto& [key,unused]:cache) { (void)unused;check(std::abs(static_cast<long long>(key.first))<100000000 && std::abs(static_cast<long long>(key.second))<100000000,"Invalid saved cache coordinate");}}
    static void fields(Archive& a,Snow& v) {
        a(v.config_,v.anchor_,v.east_,v.north_,v.originX_,v.originZ_,v.grid_,v.cache_,v.cacheOrder_,v.displayDepth_,v.totals_,v.experiment_,v.solar_,v.time_,v.driftX_,v.driftZ_,v.liquidRate_,v.fraction_,v.revision_,v.steps_,v.geometry_,v.enabled_,v.observed_,v.initialized_);
        check(v.cache_.size()<=16384 && (v.displayDepth_.size()==1024 || v.displayDepth_.empty()) && std::abs(static_cast<long long>(v.originX_))<100000000 && std::abs(static_cast<long long>(v.originZ_))<100000000,"Invalid saved snow cache");cacheBounds(v.cache_);unit(v.anchor_);unit(v.east_);unit(v.north_);
    }
    static void fields(Archive& a,Hydrology& v) {
        a(v.snow,v.config_,v.anchor_,v.east_,v.north_,v.originX_,v.originZ_,v.grid_,v.cache_,v.cacheOrder_,v.tracers_,v.totals_,v.climateWetness_,v.vegetation_,v.revision_,v.steps_,v.enabled_,v.erosion_,v.geometryMask_);
        check(v.cache_.size()<=65536 && std::abs(static_cast<long long>(v.originX_))<100000000 && std::abs(static_cast<long long>(v.originZ_))<100000000,"Invalid saved water cache");cacheBounds(v.cache_);unit(v.anchor_);unit(v.east_);unit(v.north_);
    }
    static void fields(Archive& a,WaterMotion::Component& v) {a(v.direction,v.k,v.omega,v.phase,v.variance);}
    static void fields(Archive& a,WaterMotion& v) {
        a(v.waves_,v.config_,v.anchor_,v.east_,v.north_,v.wind_,v.current_,v.probes_,v.experiment_,v.time_,v.mixedDepth_,v.fetch_,v.enabled_,v.initialized_,v.oceanHere_,v.globalFlow_);
        check(v.mixedDepth_>0 && v.fetch_>=0 && (!v.experiment_ || (*v.experiment_>=0 && *v.experiment_<=40)),"Invalid saved water motion");unit(v.anchor_);unit(v.east_);unit(v.north_);
    }
    static void fields(Archive& a,Flora& v) {a(v.days_,v.rate_,v.epochDay_,v.young_);check(v.days_>=0 && v.rate_>=1 && v.rate_<=2592000,"Invalid saved plant clock");}
    static void fields(Archive& a,DeerHabitat& v) {a(v.id,v.home,v.bank,v.water,v.outward);}
    static void fields(Archive& a,Deer& v) {a(v.id,v.up,v.previous,v.heading,v.previousHeading,v.target,v.elevation,v.previousElevation,v.speed,v.stride,v.previousStride,v.lowerHead,v.previousHead,v.foldLegs,v.previousFold,v.size,v.male,v.activity);unit(v.up);check(v.size>0 && v.size<4,"Invalid saved animal");}
    static void fields(Archive& a,DeerHerd& v) {a(v.habitat,v.members,v.obstacles,v.obstacleCenter,v.activity,v.remaining,v.thirst,v.alarm,v.decisions,v.lastSeen,v.active);check(v.members.size()>=3 && v.members.size()<=7,"Invalid saved herd size");}
    static void fields(Archive& a,Fauna& v) {a(v.herds_,v.config_,v.surveyed_,v.pending_,v.elapsed_,v.steps_,v.visit_,v.geometry_,v.initialized_,v.enabled_);check(v.herds_.size()<=Fauna::cacheLimit && v.pending_>=0 && v.pending_<Fauna::fixedStep+1e-6,"Invalid saved fauna");}
    static void fields(Archive& a,Simulation& v) {a(v.global,v.weather,v.water,v.motion,v.flora,v.fauna,v.pending_,v.experimentRain_);check(v.pending_>=0 && v.pending_<Weather::fixedStep+1e-6 && v.experimentRain_>=0 && v.experimentRain_<=200,"Invalid saved simulation clock");}
    static std::span<const std::byte> payload(std::span<const std::byte> bytes) {
        check(bytes.size()>=headerSize && bytes.size()<=maximumFile,"Invalid saved file size");
        check(std::memcmp(bytes.data(),magic.data(),magic.size())==0,"Unknown saved file signature");
        Archive h(bytes.subspan(8,headerSize-8));std::uint32_t version=0;std::uint64_t size=0,hash=0;h(version,size,hash);
        check(version==1,"Unsupported saved state version");check(size==bytes.size()-headerSize,"Truncated saved file");
        auto data=bytes.subspan(headerSize);check(checksum(data)==hash,"Saved state checksum mismatch");return data;
    }
    static std::vector<std::byte> encode(const World& w,const Explorer& explorer,const Simulation& sim,const SessionState& session,std::uint64_t savedAt) {
        SaveMetadata meta{savedAt,w.config().seed,degrees(latitude(explorer.up())),degrees(longitude(explorer.up())),session.seconds};
        auto config=w.config();auto modules=w.modules();auto e=explorer;auto simulation=sim;auto v=session;
        Archive body;body(meta,config,modules,v,e,simulation);
        Archive header;std::uint32_t version=1;std::uint64_t size=body.output.size(),hash=checksum(body.output);header(version,size,hash);
        std::vector<std::byte> bytes;for(auto b:magic)bytes.push_back(static_cast<std::byte>(b));bytes.insert(bytes.end(),header.output.begin(),header.output.end());bytes.insert(bytes.end(),body.output.begin(),body.output.end());
        check(bytes.size()<=maximumFile,"Saved file exceeds size limit");return bytes;
    }
    static std::unique_ptr<LoadedState> decode(std::span<const std::byte> bytes) {
        Archive a(payload(bytes));SaveMetadata meta;Config config;Modules modules;SessionState session;a(meta,config,modules,session);
        check(meta.seed==config.seed && meta.seconds==session.seconds,"Inconsistent saved header");
        auto state=std::make_unique<LoadedState>(config,modules,direction(radians(meta.latitude),radians(meta.longitude)),session.seconds);
        state->session=session;state->metadata=meta;a(state->explorer,state->simulation);check(a.cursor==a.input.size(),"Unexpected trailing save data");
        const auto p=state->explorer.up();check(std::abs(degrees(latitude(p))-meta.latitude)<1e-7 && std::abs(degrees(longitude(p))-meta.longitude)<1e-7,"Inconsistent saved observer");
        state->simulation.motion.circulation_=state->simulation.motion.globalFlow_ ? state->world.waterNetwork() : nullptr;
        return state;
    }
};
std::vector<std::byte> encodeState(const World& w,const Explorer& e,const Simulation& s,const SessionState& v,std::uint64_t when) {
    if(!when) when=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    return StateCodec::encode(w,e,s,v,when);
}
std::unique_ptr<LoadedState> decodeState(std::span<const std::byte> bytes) {return StateCodec::decode(bytes);}
void saveState(const std::filesystem::path& path,const World& w,const Explorer& e,const Simulation& s,const SessionState& v) {
    const auto data=encodeState(w,e,s,v);std::vector<std::byte> previous;
    if(std::filesystem::is_regular_file(path)) {
        try {previous=readBoundedFile(path,maximumFile);(void)StateCodec::payload(previous);}catch(const std::exception&) {previous.clear();}
    }
    if(!previous.empty()) atomicWriteFile(backup(path),previous);
    atomicWriteFile(path,data);
}
std::unique_ptr<LoadedState> loadState(const std::filesystem::path& path) {
    try {return decodeState(readBoundedFile(path,maximumFile));}
    catch(const std::exception& first) {
        try {auto state=decodeState(readBoundedFile(backup(path),maximumFile));state->recovered=true;return state;}
        catch(const std::exception&) {throw std::runtime_error(std::string("Cannot load state: ")+first.what());}
    }
}
std::optional<SaveMetadata> readSaveMetadata(const std::filesystem::path& path) {
    // A cheap slot label, not validation; loading verifies every byte and field.
    const auto read=[](const std::filesystem::path& p)->std::optional<SaveMetadata> {
        try {
            if(!std::filesystem::is_regular_file(p) || std::filesystem::file_size(p)>maximumFile) return {};
            std::array<std::byte,headerSize+40> bytes{};std::ifstream in(p,std::ios::binary);in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            if(!in || std::memcmp(bytes.data(),magic.data(),8)!=0)return {};
            StateCodec::Archive h(std::span<const std::byte>(bytes).subspan(8,20));std::uint32_t version=0;std::uint64_t size=0,hash=0;h(version,size,hash);
            if(version!=1 || size!=std::filesystem::file_size(p)-headerSize)return {};
            StateCodec::Archive a(std::span<const std::byte>(bytes).subspan(headerSize));SaveMetadata meta;a(meta);return meta;
        } catch(const std::exception&) {return {};}
    };
    if(auto result=read(path))return result;
    return read(backup(path));
}
std::filesystem::path slotPath(const std::filesystem::path& directory,unsigned slot) {
    if(slot>=saveSlotCount)throw std::invalid_argument("Invalid save slot");
    return directory/(slot==quickSlot ? "quick.pls" : slot==autoSlot ? "auto.pls" : "slot"+std::to_string(slot+1)+".pls");
}
} // namespace planet

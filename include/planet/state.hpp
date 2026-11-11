#pragma once
#include "planet/hydrology.hpp"
#include "planet/navigation.hpp"
#include "planet/phenomena.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <vector>
namespace planet {
inline constexpr unsigned manualSlotCount=6,quickSlot=6,autoSlot=7,saveSlotCount=8;
struct SessionState {
    double seconds=79*86400.,timeScale=1,phenomenonEpoch=0;
    bool paused=false,globe=false,help=true,headlamp=true,windTracers=false,waterTracers=false,floraInfo=true,faunaInfo=false;
    unsigned weatherLayer=0;
    Phenomenon phenomenon=Phenomenon::Natural;
    Vec3 globeOutward{0,0,1},globeUp{0,1,0};
};
struct SaveMetadata {std::uint64_t savedAt=0,seed=0;double latitude=0,longitude=0,seconds=0;};
struct LoadedState {
    World world;Explorer explorer;Simulation simulation;SessionState session;SaveMetadata metadata;
    bool recovered=false;
    LoadedState(Config config,Modules modules,Vec3 observer,double seconds);
};
// Explicit little-endian, versioned, bounded format; never dumps C++ object memory.
std::vector<std::byte> encodeState(const World& world,const Explorer& explorer,const Simulation& simulation,const SessionState& session,std::uint64_t savedAt=0);
std::unique_ptr<LoadedState> decodeState(std::span<const std::byte> bytes);
void saveState(const std::filesystem::path& path,const World&,const Explorer&,const Simulation&,const SessionState&);
std::unique_ptr<LoadedState> loadState(const std::filesystem::path& path);
std::optional<SaveMetadata> readSaveMetadata(const std::filesystem::path& path);
std::filesystem::path slotPath(const std::filesystem::path& directory,unsigned slot);
}

#pragma once
#include "planet/hydrology.hpp"
#include "planet/navigation.hpp"
#include <filesystem>

namespace planet {
struct AcousticStrike {
    std::uint64_t key=0;
    double distance=0,pan=0,strength=0,age=0;
    Vec3 source{};
};
struct SoundScene {
    std::uint64_t seed=42;
    Vec3 listener{};
    Vec3 right{1,0,0};
    bool audible=false,underwater=false,sheltered=false;
    bool thunderEnabled=true;
    double volume=.55,wind=0,rain=0,water=0,rustle=0,windPan=0,waterPan=0;
    double temperature=288.15;
    std::vector<AcousticStrike> strikes;
};
// Sound levels are perceptual controls, not calibrated sound-pressure levels.
SoundScene soundScene(const World& world,const Simulation& simulation,const Explorer& explorer,
                      double volume=.55,bool paused=false,bool globe=false);
double speedOfSound(double temperatureK);
double thunderDelay(double distanceMetres,double temperatureK);
class SoundSynth {
public:
    static constexpr unsigned sampleRate=48000,channels=2;
    explicit SoundSynth(std::uint64_t seed=42);
    void setScene(const SoundScene& scene);
    void render(std::span<std::int16_t> interleavedStereo);
    std::uint64_t thunderCount() const { return thunderCount_; }
    std::size_t activeThunder() const;
private:
    struct Channel { double windLow=0,windHigh=0,rainLow=0,rainHigh=0,seaLow=0,seaHigh=0,under=0,cave=0,dc=0; };
    struct Voice { bool active=false;std::uint64_t delay=0,age=0,duration=0;double amplitude=0,pan=0,cutoff=0,low=0,rumble=0;std::uint32_t rng=1;Vec3 source{}; };
    void reset(std::uint64_t seed);
    static double noise(std::uint32_t& state);
    SoundScene scene_;
    std::array<Channel,2> filters_{};
    std::array<Voice,8> voices_{};
    std::array<std::uint64_t,32> seen_{};
    std::size_t nextSeen_=0,seenCount_=0;
    std::array<std::uint32_t,2> rng_{};
    double volume_=0,wind_=0,rain_=0,water_=0,rustle_=0,under_=0,shelter_=0,gust_=0,phase_=0;
    std::uint64_t samples_=0,thunderCount_=0;
    bool initialized_=false;
};
void writeWave(const std::filesystem::path& path,std::span<const std::int16_t> stereo);
} // namespace planet

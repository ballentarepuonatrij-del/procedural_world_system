#pragma once
#include "planet/sound.hpp"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace planet {
// Only the worker owns WinMM buffers and DSP state. Main thread publishes scenes.
class WavePlayer {
public:
    explicit WavePlayer(bool record=false);
    ~WavePlayer();
    WavePlayer(const WavePlayer&)=delete;
    WavePlayer& operator=(const WavePlayer&)=delete;
    void update(const SoundScene& scene);
    void stop();
    void saveCapture(const std::filesystem::path& path);
    bool available() const {return available_;}
    unsigned error() const {return error_;}
    std::uint64_t frames() const {return frames_;}
    std::uint64_t submitted() const {return submitted_;}
    std::uint64_t completed() const {return completed_;}
    std::uint64_t thunderCount() const {return thunderCount_;}
    unsigned peak() const {return peak_;}
private:
    void run();
    SoundScene scene_;
    std::mutex mutex_;
    std::thread worker_;
    HANDLE wake_=nullptr;
    std::atomic<bool> stop_{false},available_{false};
    std::atomic<unsigned> error_{0},peak_{0};
    std::atomic<std::uint64_t> frames_{0},submitted_{0},completed_{0},thunderCount_{0};
    bool record_=false;
    std::vector<std::int16_t> recording_;
};
} // namespace planet

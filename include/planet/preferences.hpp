#pragma once
#include "planet/model.hpp"
#include <filesystem>
#include <string>

namespace planet {
struct Preferences {
    Modules modules;
    bool grid=false,interfaceVisible=true;
    bool sky=true,shadows=true;
    unsigned volume=55;
    unsigned gamma=100,brightness=100;
    std::uint64_t seed=42;
};
struct WindowLayout {int x,y,width,height;};
WindowLayout defaultWindowLayout(int left,int top,int width,int height);
double displayIntensity(double value,unsigned gamma,unsigned brightness);
std::optional<std::uint64_t> parseSeed(std::string_view text);
Preferences parsePreferences(std::string_view text);
std::string serializePreferences(const Preferences& preferences);
struct PreferencesRead { Preferences value; std::string warning; };
PreferencesRead loadPreferences(const std::filesystem::path& path);
// Replace the old file only after a complete temporary file has been written.
void savePreferences(const std::filesystem::path& path,const Preferences& preferences);
} // namespace planet

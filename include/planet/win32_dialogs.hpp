#pragma once
#include "planet/preferences.hpp"
#include <windows.h>
#include <functional>

namespace planet {
std::filesystem::path defaultPreferencesPath();
enum class SettingsResult {Cancelled,Applied,Loaded};
struct SlotInfo {std::wstring description;bool available=false;};
struct SettingsActions {
    std::function<void(const Preferences&)> commit,preview;
    std::function<SlotInfo(unsigned)> slot;
    std::function<void(unsigned)> save,load;
};
SettingsResult settingsDialog(HWND owner,const Preferences& initial,const SettingsActions& actions);
bool seedDialog(HWND owner,std::uint64_t initial,const std::function<void(std::uint64_t)>& commit);
} // namespace planet

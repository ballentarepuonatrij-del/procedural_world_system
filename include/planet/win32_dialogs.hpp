#pragma once
#include "planet/preferences.hpp"
#include <windows.h>
#include <functional>

namespace planet {
std::filesystem::path defaultPreferencesPath();
bool settingsDialog(HWND owner,const Preferences& initial,const std::function<void(const Preferences&)>& commit);
bool seedDialog(HWND owner,std::uint64_t initial,const std::function<void(std::uint64_t)>& commit);
} // namespace planet

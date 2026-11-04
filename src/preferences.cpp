#include "planet/preferences.hpp"
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace planet {
namespace {
constexpr std::array<std::string_view,moduleCount> keys{"terrain","ocean","atmosphere","climate","biomes","detail","wind","clouds","water","erosion","landforms","caves","water_motion","plant_growth","global_weather","sound"};
std::string_view trim(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==text.npos) return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
}
std::optional<std::uint64_t> parseSeed(std::string_view text) {
    if(text.empty() || text.find_first_not_of("0123456789")!=text.npos) return std::nullopt;
    std::uint64_t value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size()) return std::nullopt;
    return value;
}
Preferences parsePreferences(std::string_view text) {
    if(text.size()>16384) throw std::invalid_argument("Settings file is too large");
    Preferences preferences;bool version=false;
    while(!text.empty()) {
        const auto newline=text.find('\n');const auto line=trim(text.substr(0,newline));
        text=newline==text.npos ? std::string_view{} : text.substr(newline+1);
        if(line.empty() || line.front()=='#' || line.front()==';') continue;
        const auto equals=line.find('=');
        if(equals==line.npos) throw std::invalid_argument("Invalid settings line");
        const auto key=trim(line.substr(0,equals)),value=trim(line.substr(equals+1));
        if(key=="version") { if(value!="1") throw std::invalid_argument("Unsupported settings version");version=true; }
        else if(key=="seed") {
            const auto seed=parseSeed(value);if(!seed) throw std::invalid_argument("Invalid saved seed");preferences.seed=*seed;
        } else if(key=="volume") {
            const auto volume=parseSeed(value);if(!volume || *volume>100) throw std::invalid_argument("Invalid saved volume");
            preferences.volume=static_cast<unsigned>(*volume);
        } else {
            const auto module=std::find(keys.begin(),keys.end(),key);
            if(key!="grid" && key!="interface" && key!="sky" && key!="shadows" && module==keys.end()) continue;
            if(value!="0" && value!="1") throw std::invalid_argument("Invalid settings switch");
            const bool enabled=value=="1";
            if(key=="grid") preferences.grid=enabled;
            else if(key=="interface") preferences.interfaceVisible=enabled;
            else if(key=="sky") preferences.sky=enabled;
            else if(key=="shadows") preferences.shadows=enabled;
            else preferences.modules.set(static_cast<Module>(module-keys.begin()),enabled);
        }
    }
    if(!version) throw std::invalid_argument("Settings version is missing");
    return preferences;
}
std::string serializePreferences(const Preferences& p) {
    if(p.volume>100) throw std::invalid_argument("Volume must be 0..100");
    std::ostringstream out;out << "version=1\nseed=" << p.seed << "\ngrid=" << p.grid
        << "\ninterface=" << p.interfaceVisible << "\nvolume=" << p.volume << "\nsky=" << p.sky << "\nshadows=" << p.shadows << '\n';
    for(unsigned i=0;i<moduleCount;++i) out << keys[i] << '=' << p.modules.requested(static_cast<Module>(i)) << '\n';
    return out.str();
}
PreferencesRead loadPreferences(const std::filesystem::path& path) {
    try {
        if(!std::filesystem::exists(path)) return {};
        if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)>16384)
            throw std::runtime_error("Settings must be a regular file of at most 16 KiB");
        std::ifstream in(path,std::ios::binary);if(!in) throw std::runtime_error("Cannot open settings");
        std::array<char,16385> buffer{};in.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
        if(in.bad()) throw std::runtime_error("Cannot read settings");
        return {parsePreferences(std::string_view(buffer.data(),static_cast<std::size_t>(in.gcount()))),{}};
    } catch(const std::exception& e) {return {{},e.what()};}
}
void savePreferences(const std::filesystem::path& path,const Preferences& preferences) {
    const auto text=serializePreferences(preferences);
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    static std::atomic<unsigned long long> sequence=0;
    auto temporary=path;temporary+=std::filesystem::path(".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++));
    try {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        if(!out) throw std::runtime_error("Cannot create temporary settings file");
        out.write(text.data(),static_cast<std::streamsize>(text.size()));out.flush();out.close();
        if(!out) throw std::runtime_error("Cannot write settings file");
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace settings file (Windows error "+std::to_string(GetLastError())+")");
#else
        std::filesystem::rename(temporary,path);
#endif
    } catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
} // namespace planet

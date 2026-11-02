#include "planet/model.hpp"
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
double number(const std::string& text) {
    std::size_t used = 0;
    const double value = std::stod(text,&used);
    if (used != text.size() || !std::isfinite(value)) throw std::invalid_argument("Invalid number: " + text);
    return value;
}
}
int main(int argc, char** argv) {
    try {
        planet::Config config;
        double lat = 30, lon = 0, day = 79;
        std::string mapPath;
        bool sample = false;
        for (int i=1; i<argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() -> std::string {
                if (++i >= argc) throw std::invalid_argument("Missing value for " + arg);
                return argv[i];
            };
            if (arg == "--seed") {
                const auto value = next();
                const auto [end,error] = std::from_chars(value.data(),value.data()+value.size(),config.seed);
                if (error != std::errc{} || end != value.data()+value.size()) throw std::invalid_argument("Invalid seed");
            } else if (arg == "--sample") { lat = number(next()); lon = number(next()); sample = true; }
            else if (arg == "--day") day = number(next());
            else if (arg == "--map") mapPath = next();
            else if (arg == "--help") {
                std::cout << "planet_cli [--seed uint64] [--sample latitude longitude] [--day days_since_perihelion] [--map world.ppm]\n";
                return 0;
            } else throw std::invalid_argument("Unknown option: " + arg);
        }
        if (std::abs(lat)>90 || std::abs(lon)>180 || std::abs(day)>1e7) throw std::invalid_argument("Coordinates or time outside supported range");
        const planet::World world(config);
        if (sample || mapPath.empty()) {
            const auto s = world.sample(planet::direction(planet::radians(lat),planet::radians(lon)),day*86400);
            std::cout << std::fixed << std::setprecision(6)
                      << "{\n  \"seed\": " << config.seed
                      << ",\n  \"latitude_deg\": " << lat << ",\n  \"longitude_deg\": " << lon
                      << ",\n  \"elevation_m\": " << s.elevation
                      << ",\n  \"temperature_K\": " << s.temperature
                      << ",\n  \"pressure_Pa\": " << s.pressure
                      << ",\n  \"density_kg_m3\": " << s.density
                      << ",\n  \"annual_precipitation_mm\": " << s.precipitation
                      << ",\n  \"insolation_W_m2\": " << s.insolation
                      << ",\n  \"biome\": \"" << planet::biomeName(s.biome) << "\"\n}\n";
        }
        if (!mapPath.empty()) {
            constexpr int width = 1024, height = 512;
            std::ofstream file(mapPath,std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open output map");
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
                const auto p = planet::direction(planet::pi/2-(y+0.5)*planet::pi/height,(x+0.5)*planet::tau/width-planet::pi);
                const auto s = world.sample(p,day*86400);
                auto color = world.color(s);
                if (s.ocean) {
                    const double depth = planet::clamp(-s.elevation/5000,0.0,1.0);
                    color = {0.03f,static_cast<float>(0.32-0.19*depth),static_cast<float>(0.43-0.19*depth)};
                }
                const unsigned char rgb[]{static_cast<unsigned char>(planet::clamp(color.r,0.0f,1.0f)*255),
                                          static_cast<unsigned char>(planet::clamp(color.g,0.0f,1.0f)*255),
                                          static_cast<unsigned char>(planet::clamp(color.b,0.0f,1.0f)*255)};
                file.write(reinterpret_cast<const char*>(rgb),3);
            }
            if (!file) throw std::runtime_error("Map write failed");
            std::cerr << "Wrote 1024 x 512 biome map: " << mapPath << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "planet_cli: " << error.what() << '\n';
        return 1;
    }
}

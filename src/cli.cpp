#include "planet/weather.hpp"
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
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
        std::optional<double> weatherSeconds;
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
            else if (arg == "--weather") {
                weatherSeconds=number(next());
                if(*weatherSeconds<0 || *weatherSeconds>3600) throw std::invalid_argument("Weather duration must be in [0,3600] seconds");
            }
            else if (arg == "--help") {
                std::cout << "planet_cli [--seed uint64] [--sample latitude longitude] [--day days_since_perihelion] [--map world.ppm] [--weather 0..3600_seconds]\n";
                return 0;
            } else throw std::invalid_argument("Unknown option: " + arg);
        }
        if (std::abs(lat)>90 || std::abs(lon)>180 || std::abs(day)>1e7) throw std::invalid_argument("Coordinates or time outside supported range");
        const planet::World world(config);
        const auto location=planet::direction(planet::radians(lat),planet::radians(lon));
        const double finalTime=day*86400+weatherSeconds.value_or(0);
        if (sample || mapPath.empty() || weatherSeconds) {
            const auto s = world.sample(location,finalTime);
            std::cout << std::fixed << std::setprecision(6)
                      << "{\n  \"seed\": " << config.seed
                      << ",\n  \"latitude_deg\": " << lat << ",\n  \"longitude_deg\": " << lon
                      << ",\n  \"elevation_m\": " << s.elevation
                      << ",\n  \"temperature_K\": " << s.temperature
                      << ",\n  \"pressure_Pa\": " << s.pressure
                      << ",\n  \"density_kg_m3\": " << s.density
                      << ",\n  \"annual_precipitation_mm\": " << s.precipitation
                      << ",\n  \"insolation_W_m2\": " << s.insolation
                      << ",\n  \"biome\": \"" << planet::biomeName(s.biome) << '"';
            if(weatherSeconds) {
                planet::Weather weather(world,location,day*86400);
                for(double remaining=*weatherSeconds;remaining>1e-9;) {
                    const double step=std::min(60.0,remaining); weather.advance(world,location,step); remaining-=step;
                }
                const auto air=weather.sample(location); const auto water=weather.budget();
                std::cout << ",\n  \"weather\": {\n    \"elapsed_s\": " << *weatherSeconds
                          << ",\n    \"layer_altitude_m\": " << weather.layerAltitude()
                          << ",\n    \"wind_east_m_s\": " << air.wind.east << ",\n    \"wind_north_m_s\": " << air.wind.north
                          << ",\n    \"relative_humidity\": " << air.humidity << ",\n    \"cloud_water_kg_m2\": " << air.cloudWater
                          << ",\n    \"rain_mm_h\": " << air.rainRate*3600
                          << ",\n    \"stored_water_kg\": " << water.storedKg << ",\n    \"evaporated_kg\": " << water.evaporatedKg
                          << ",\n    \"precipitated_kg\": " << water.precipitatedKg << ",\n    \"budget_error_kg\": " << water.residualKg() << "\n  }";
            }
            std::cout << "\n}\n";
        }
        if (!mapPath.empty()) {
            constexpr int width = 1024, height = 512;
            std::ofstream file(mapPath,std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open output map");
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
                const auto p = planet::direction(planet::pi/2-(y+0.5)*planet::pi/height,(x+0.5)*planet::tau/width-planet::pi);
                const auto s = world.sample(p,finalTime);
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

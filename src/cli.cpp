#include "planet/hydrology.hpp"
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
        std::optional<double> weatherSeconds,waterSeconds;
        double rainMmH=0;
        std::optional<planet::Landform> landmark;
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
            else if (arg == "--water") {
                waterSeconds=number(next());
                if(*waterSeconds<0 || *waterSeconds>3600) throw std::invalid_argument("Water duration must be in [0,3600] seconds");
            }
            else if (arg == "--rain-mm-h") {
                rainMmH=number(next());
                if(rainMmH<0 || rainMmH>200) throw std::invalid_argument("Rain experiment must be in [0,200] mm/h");
            }
            else if(arg=="--landmark") {
                landmark=planet::landmarkType(next());
                if(!landmark) throw std::invalid_argument("Landmark must be cave, canyon, mountains or gorge");
            }
            else if (arg == "--help") {
                std::cout << "planet_cli [--seed uint64] [--sample latitude longitude] [--day days_since_perihelion] [--map world.ppm] [--weather 0..3600_seconds] [--water 0..3600_seconds] [--rain-mm-h 0..200] [--landmark cave|canyon|mountains|gorge]\n";
                return 0;
            } else throw std::invalid_argument("Unknown option: " + arg);
        }
        if (std::abs(lat)>90 || std::abs(lon)>180 || std::abs(day)>1e7) throw std::invalid_argument("Coordinates or time outside supported range");
        if(weatherSeconds && waterSeconds && *weatherSeconds!=*waterSeconds)
            throw std::invalid_argument("Weather and water share one duration; supply the same seconds");
        if(rainMmH>0 && !waterSeconds) throw std::invalid_argument("--rain-mm-h requires --water duration");
        const double duration=waterSeconds.value_or(weatherSeconds.value_or(0));
        const planet::World world(config);
        if(landmark) {
            const auto place=world.findLandmark(*landmark,world.findLand());
            lat=planet::degrees(planet::latitude(place.position)); lon=planet::degrees(planet::longitude(place.position)); sample=true;
        }
        const auto location=planet::direction(planet::radians(lat),planet::radians(lon));
        const double finalTime=day*86400+duration;
        if (sample || mapPath.empty() || weatherSeconds || waterSeconds) {
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
                      << ",\n  \"landform\": \"" << planet::landformName(s.landform) << '"'
                      << ",\n  \"biome\": \"" << planet::biomeName(s.biome) << '"';
            if(weatherSeconds || waterSeconds) {
                planet::Simulation simulation(world,location,day*86400); simulation.setRainExperiment(rainMmH);
                for(double remaining=duration;remaining>1e-9;) {
                    const double step=std::min(60.0,remaining); simulation.advance(world,location,step); remaining-=step;
                }
                const auto& weather=simulation.weather;
                const auto air=weather.sample(location); const auto water=weather.budget();
                std::cout << ",\n  \"weather\": {\n    \"elapsed_s\": " << duration
                          << ",\n    \"layer_altitude_m\": " << weather.layerAltitude()
                          << ",\n    \"wind_east_m_s\": " << air.wind.east << ",\n    \"wind_north_m_s\": " << air.wind.north
                          << ",\n    \"relative_humidity\": " << air.humidity << ",\n    \"cloud_water_kg_m2\": " << air.cloudWater
                          << ",\n    \"rain_mm_h\": " << air.rainRate*3600
                          << ",\n    \"stored_water_kg\": " << water.storedKg << ",\n    \"evaporated_kg\": " << water.evaporatedKg
                          << ",\n    \"precipitated_kg\": " << water.precipitatedKg << ",\n    \"budget_error_kg\": " << water.residualKg() << "\n  }";
                if(waterSeconds) {
                    const auto ground=simulation.water.sample(location); const auto runoff=simulation.water.budget();
                    double maximumDepth=0,maximumChange=0;
                    for(const auto& c:simulation.water.grid().cells) if(!c.sea && !c.masked) {
                        maximumDepth=std::max(maximumDepth,c.water);
                        maximumChange=std::max(maximumChange,std::abs(c.soil-c.initialSoil));
                    }
                    std::cout << ",\n  \"surface_water\": {\n    \"elapsed_s\": " << duration
                        << ",\n    \"experiment_rain_mm_h\": " << rainMmH
                        << ",\n    \"depth_m\": " << ground.depth << ",\n    \"maximum_depth_m\": " << maximumDepth
                        << ",\n    \"flow_east_m_s\": " << ground.velocity.east << ",\n    \"flow_north_m_s\": " << ground.velocity.north
                        << ",\n    \"soil_saturation\": " << ground.saturation << ",\n    \"bed_change_m\": " << ground.bedChange
                        << ",\n    \"maximum_bed_change_m\": " << maximumChange << ",\n    \"stored_water_m3\": " << runoff.storedWaterM3
                        << ",\n    \"rain_m3\": " << runoff.rainM3 << ",\n    \"evaporated_m3\": " << runoff.evaporatedM3
                        << ",\n    \"drained_m3\": " << runoff.drainedM3 << ",\n    \"boundary_water_m3\": " << runoff.boundaryM3
                        << ",\n    \"water_error_m3\": " << runoff.waterResidual() << ",\n    \"solid_error_kg\": " << runoff.solidResidual()
                        << ",\n    \"eroded_kg\": " << runoff.erodedKg << ",\n    \"deposited_kg\": " << runoff.depositedKg << "\n  }";
                }
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

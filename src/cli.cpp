#include "planet/hydrology.hpp"
#include "planet/surface_detail.hpp"
#include "planet/sound.hpp"
#include "planet/rivers.hpp"
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
        std::optional<double> waterWind;
        double rainMmH=0;
        bool floraOutput=false,youngFlora=false,grove=false;
        double growDays=0;
        double globalHours=0;
        bool globalOutput=false,globalEnabled=true;
        std::optional<planet::StormKind> storm;
        std::optional<planet::Landform> landmark;
        std::string mapPath;
        std::string audioPath;
        double audioSeconds=10,volume=.55;
        bool audioDurationSet=false,soundEnabled=true;
        bool sample = false,coast=false;
        bool riverOutput=false,riversEnabled=true;std::optional<planet::RiverKind> riverSite;
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
            else if(arg=="--rivers") riverOutput=true;
            else if(arg=="--no-rivers") {riversEnabled=false;riverOutput=true;}
            else if(arg=="--river") {riverSite=planet::riverKind(next());riverOutput=true;if(!riverSite) throw std::invalid_argument("River must be stream, river or major");}
            else if (arg == "--day") day = number(next());
            else if (arg == "--map") mapPath = next();
            else if(arg=="--audio") audioPath=next();
            else if(arg=="--no-sound") soundEnabled=false;
            else if(arg=="--volume") {
                volume=number(next())/100;
                if(volume<0 || volume>1) throw std::invalid_argument("Volume must be 0..100 percent");
            }
            else if(arg=="--audio-seconds") {
                audioSeconds=number(next());audioDurationSet=true;
                if(audioSeconds<.1 || audioSeconds>60) throw std::invalid_argument("Audio duration must be 0.1..60 seconds");
            }
            else if (arg == "--coast") coast=true;
            else if(arg=="--no-global-weather") {globalEnabled=false;globalOutput=true;}
            else if(arg=="--storm") {
                storm=planet::stormKind(next());globalOutput=true;
                if(!storm) throw std::invalid_argument("Storm must be cyclone, hurricane or thunderstorm");
            }
            else if(arg=="--global-hours") {
                globalHours=number(next());globalOutput=true;
                if(globalHours<0 || globalHours>240) throw std::invalid_argument("Global preview must be 0..240 hours");
            }
            else if(arg=="--flora") floraOutput=true;
            else if(arg=="--grove") { grove=true;floraOutput=true; }
            else if(arg=="--young-flora") { youngFlora=true;floraOutput=true; }
            else if(arg=="--grow-days") {
                growDays=number(next());floraOutput=true;
                if(growDays<0 || growDays>365250) throw std::invalid_argument("Growth days must be 0..365250");
            }
            else if (arg == "--wind-m-s") {
                waterWind=number(next());
                if(*waterWind<0 || *waterWind>40) throw std::invalid_argument("Water wind must be in [0,40] m/s");
            }
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
                std::cout << "planet_cli [--seed uint64] [--sample latitude longitude] [--day days_since_perihelion] [--map world.ppm] [--weather 0..3600_seconds] [--water 0..3600_seconds] [--rain-mm-h 0..200] [--landmark cave|canyon|mountains|gorge] [--coast] [--wind-m-s 0..40] [--flora] [--grove] [--young-flora] [--grow-days 0..365250]\n";
                std::cout << "Global weather: [--storm cyclone|hurricane|thunderstorm] [--global-hours 0..240] [--no-global-weather]\n";
                std::cout << "Global water: [--rivers] [--river stream|river|major] [--no-rivers]\n";
                std::cout << "Procedural audio: [--audio output.wav] [--audio-seconds 0.1..60] [--volume 0..100] [--no-sound]\n";
                return 0;
            } else throw std::invalid_argument("Unknown option: " + arg);
        }
        if (std::abs(lat)>90 || std::abs(lon)>180 || std::abs(day)>1e7) throw std::invalid_argument("Coordinates or time outside supported range");
        if(weatherSeconds && waterSeconds && *weatherSeconds!=*waterSeconds)
            throw std::invalid_argument("Weather and water share one duration; supply the same seconds");
        if(rainMmH>0 && !waterSeconds) throw std::invalid_argument("--rain-mm-h requires --water duration");
        if(audioDurationSet && audioPath.empty()) throw std::invalid_argument("--audio-seconds requires --audio output.wav");
        const double duration=waterSeconds.value_or(weatherSeconds.value_or(0));
        planet::World world(config);world.modules().set(planet::Module::GlobalWeather,globalEnabled);
        world.modules().set(planet::Module::Sound,soundEnabled);
        world.modules().set(planet::Module::Rivers,riversEnabled);
        if(floraOutput && !sample && !coast && !landmark) {
            const auto p=world.findLand();lat=planet::degrees(planet::latitude(p));lon=planet::degrees(planet::longitude(p));sample=true;
        }
        if(landmark) {
            const auto place=world.findLandmark(*landmark,world.findLand());
            lat=planet::degrees(planet::latitude(place.position)); lon=planet::degrees(planet::longitude(place.position)); sample=true;
        }
        if(coast) {
            const auto p=world.findCoast().lookToward;
            lat=planet::degrees(planet::latitude(p)); lon=planet::degrees(planet::longitude(p)); sample=true;
        }
        if(grove && !coast) {
            const auto p=planet::findGrove(world).position;
            lat=planet::degrees(planet::latitude(p));lon=planet::degrees(planet::longitude(p));sample=true;
        }
        if(riverSite) {
            if(!riversEnabled) throw std::invalid_argument("--river requires enabled rivers");
            const auto p=world.waterNetwork()->find(*riverSite,world.findLand()).center;
            lat=planet::degrees(planet::latitude(p));lon=planet::degrees(planet::longitude(p));sample=true;
        }
        auto location=planet::direction(planet::radians(lat),planet::radians(lon));
        std::optional<planet::Simulation> model;
        double previewSeconds=0;
        if(weatherSeconds || waterSeconds || waterWind || coast || globalOutput || !audioPath.empty()) {
            model.emplace(world,location,day*86400,waterWind,globalHours);previewSeconds=model->global.elapsed();
            if(storm) {
                if(!model->global.strongest(*storm)) throw std::runtime_error("No active storm of the requested kind on this planet");
                location=model->global.observation(*storm);lat=planet::degrees(planet::latitude(location));lon=planet::degrees(planet::longitude(location));
                model->advance(world,location,0);
            }
            model->setRainExperiment(rainMmH);
            for(double remaining=duration;remaining>1e-9;) {
                const double step=std::min(60.0,remaining);model->advance(world,location,step);remaining-=step;
            }
        }
        std::uint64_t audioFrames=0,thunderEvents=0;unsigned audioPeak=0;
        if(!audioPath.empty()) {
            planet::Explorer listener(world);listener.teleport(world,location,&model->water);
            planet::SoundSynth synth(config.seed);audioFrames=static_cast<std::uint64_t>(std::llround(audioSeconds*planet::SoundSynth::sampleRate));
            std::vector<std::int16_t> pcm(static_cast<std::size_t>(audioFrames*2));
            for(std::size_t offset=0;offset<pcm.size();) {
                const auto count=std::min<std::size_t>(2048,pcm.size()-offset);
                synth.setScene(planet::soundScene(world,*model,listener,volume));synth.render(std::span(pcm).subspan(offset,count));
                model->advance(world,location,static_cast<double>(count)/(2*planet::SoundSynth::sampleRate));offset+=count;
            }
            for(const auto value:pcm) audioPeak=std::max(audioPeak,static_cast<unsigned>(std::abs(static_cast<int>(value))));
            thunderEvents=synth.thunderCount();planet::writeWave(audioPath,pcm);
        }
        const double totalDuration=duration+static_cast<double>(audioFrames)/planet::SoundSynth::sampleRate;
        const double finalTime=day*86400+previewSeconds+totalDuration;
        if (sample || mapPath.empty() || model) {
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
            if(riverOutput) {
                const auto atlas=world.waterNetwork();const auto river=riversEnabled ? atlas->sample(location) : planet::RiverSample{};
                const auto current=atlas->oceanCurrent(location);
                std::cout << ",\n  \"rivers\": {\"enabled\": " << (riversEnabled ? "true" : "false")
                    << ", \"wet\": " << (river.wet ? "true" : "false") << ", \"reaches\": " << atlas->reaches().size()
                    << ", \"runoff_m3_s\": " << atlas->runoff() << ", \"outlet_discharge_m3_s\": " << atlas->oceanDischarge()
                    << ", \"width_m\": " << river.width << ", \"depth_m\": " << river.depth << ", \"level_m\": " << river.level
                    << ", \"discharge_m3_s\": " << river.discharge << ", \"speed_m_s\": " << river.speed
                    << ", \"flow_east_m_s\": " << planet::dot(river.downstream,planet::east(location))*river.speed
                    << ", \"flow_north_m_s\": " << planet::dot(river.downstream,planet::north(location))*river.speed
                    << ", \"ocean_current_m_s\": " << planet::length(current) << ", \"circulation_residual\": " << atlas->circulationResidual() << '}';
            }
            if(!audioPath.empty()) std::cout << ",\n  \"audio\": {\"sample_rate_hz\": " << planet::SoundSynth::sampleRate
                << ", \"channels\": 2, \"frames\": " << audioFrames << ", \"peak_pcm\": " << audioPeak
                << ", \"thunder_events\": " << thunderEvents << ", \"volume\": " << volume << '}';
            if(floraOutput) {
                planet::Flora flora;flora.setYoung(youngFlora);flora.advanceDays(world,std::min(365250.0,growDays+totalDuration/86400));
                auto plants=planet::surfaceObjects(world,location,80,true,&flora);
                std::erase_if(plants,[](const auto& o) { return o.species==planet::Species::None; });
                std::sort(plants.begin(),plants.end(),[&](const auto& a,const auto& b) { return planet::length(a.up-location)<planet::length(b.up-location); });
                std::array<unsigned,planet::speciesCount> counts{};
                double heightSum=0;unsigned marine=0;
                for(const auto& p:plants) { ++counts[static_cast<unsigned>(p.species)];heightSum+=p.height;if(planet::plantTraits(p.species).aquatic) ++marine; }
                std::cout << ",\n  \"flora\": {\n    \"elapsed_days\": " << flora.days() << ",\n    \"plants\": " << plants.size()
                    << ",\n    \"marine_plants\": " << marine << ",\n    \"height_sum_m\": " << heightSum << ",\n    \"species\": [";
                bool comma=false;
                for(unsigned i=1;i<counts.size();++i) if(counts[i]) {
                    if(comma) std::cout << ',';
                    std::cout << "{\"name\": \"" << planet::plantTraits(static_cast<planet::Species>(i)).name << "\", \"count\": " << counts[i] << '}';comma=true;
                }
                std::cout << "],\n    \"nearest\": [";
                for(std::size_t i=0;i<std::min<std::size_t>(plants.size(),8);++i) {
                    const auto& p=plants[i];if(i) std::cout << ',';
                    std::cout << "{\"site_id\": \"" << p.id << "\", \"species\": \"" << planet::plantTraits(p.species).name
                        << "\", \"stage\": \"" << planet::stageName(p.plant.stage) << "\", \"age_days\": " << p.plant.ageDays
                        << ", \"height_m\": " << p.height << ", \"generation\": " << p.plant.generation << '}';
                }
                std::cout << "]\n  }";
            }
            if(model) {
                const auto& simulation=*model;const auto& global=simulation.global;
                const auto synoptic=global.sample(location);const auto globalWater=global.budget();
                std::cout << ",\n  \"global_weather\": {\n    \"enabled\": " << (global.enabled() ? "true" : "false")
                    << ",\n    \"elapsed_s\": " << global.elapsed() << ",\n    \"steps\": " << global.steps()
                    << ",\n    \"sea_level_pressure_Pa\": " << synoptic.pressure << ",\n    \"wind_10m_m_s\": " << planet::length(synoptic.velocity)
                    << ",\n    \"severity\": " << synoptic.severity << ",\n    \"convection\": " << synoptic.convection
                    << ",\n    \"stored_water_kg\": " << globalWater.storedKg << ",\n    \"budget_error_kg\": " << globalWater.residualKg()
                    << ",\n    \"systems\": [";
                for(std::size_t i=0;i<global.storms().size();++i) {
                    const auto& system=global.storms()[i];if(i) std::cout << ',';
                    std::cout << "{\"id\": \"" << system.id << "\", \"kind\": \"" << planet::stormName(system.kind)
                        << "\", \"latitude_deg\": " << planet::degrees(planet::latitude(system.center))
                        << ", \"longitude_deg\": " << planet::degrees(planet::longitude(system.center))
                        << ", \"age_h\": " << system.age/3600 << ", \"pressure_amplitude_Pa\": " << system.deficit << '}';
                }
                std::cout << "]\n  }";
                const auto& weather=simulation.weather;
                const auto liquid=planet::liquidAt(world,&simulation.water,&simulation.motion,location);
                std::cout << ",\n  \"water_motion\": {\n    \"wind_10m_m_s\": " << planet::length(simulation.motion.forcingWind())
                          << ",\n    \"significant_wave_height_m\": " << simulation.motion.significantHeight()
                          << ",\n    \"surface_elevation_m\": " << liquid.level << ",\n    \"water_depth_m\": " << liquid.depth
                          << ",\n    \"current_east_m_s\": " << planet::dot(liquid.current,planet::east(location))
                          << ",\n    \"current_north_m_s\": " << planet::dot(liquid.current,planet::north(location)) << "\n  }";
                const auto air=weather.sample(location); const auto water=weather.budget();
                std::cout << ",\n  \"weather\": {\n    \"elapsed_s\": " << totalDuration
                          << ",\n    \"layer_altitude_m\": " << weather.layerAltitude()
                          << ",\n    \"wind_east_m_s\": " << air.wind.east << ",\n    \"wind_north_m_s\": " << air.wind.north
                          << ",\n    \"relative_humidity\": " << air.humidity << ",\n    \"cloud_water_kg_m2\": " << air.cloudWater
                          << ",\n    \"rain_mm_h\": " << air.rainRate*3600
                          << ",\n    \"stored_water_kg\": " << water.storedKg << ",\n    \"evaporated_kg\": " << water.evaporatedKg
                          << ",\n    \"domain_exchange_kg\": " << water.domainExchangeKg << ",\n    \"synoptic_exchange_kg\": " << water.synopticExchangeKg
                          << ",\n    \"precipitated_kg\": " << water.precipitatedKg << ",\n    \"budget_error_kg\": " << water.residualKg() << "\n  }";
                if(waterSeconds) {
                    const auto ground=simulation.water.sample(location); const auto runoff=simulation.water.budget();
                    double maximumDepth=0,maximumChange=0;
                    for(const auto& c:simulation.water.grid().cells) if(!c.sea && !c.masked) {
                        maximumDepth=std::max(maximumDepth,c.water);
                        maximumChange=std::max(maximumChange,std::abs(c.soil-c.initialSoil));
                    }
                    std::cout << ",\n  \"surface_water\": {\n    \"elapsed_s\": " << totalDuration
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

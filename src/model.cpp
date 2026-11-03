#include "planet/model.hpp"
#include <limits>
#include <stdexcept>

namespace planet {
double Noise::value(Vec3 p) const {
    const auto ix = static_cast<std::int64_t>(std::floor(p.x));
    const auto iy = static_cast<std::int64_t>(std::floor(p.y));
    const auto iz = static_cast<std::int64_t>(std::floor(p.z));
    const auto fade = [](double t) { return t*t*t*(t*(t*6-15)+10); };
    const double u = fade(p.x-static_cast<double>(ix));
    const double v = fade(p.y-static_cast<double>(iy));
    const double w = fade(p.z-static_cast<double>(iz));
    const auto at = [&](int x, int y, int z) {
        auto h = hash64(static_cast<std::uint64_t>(ix+x) ^ seed_);
        h = hash64(h ^ static_cast<std::uint64_t>(iy+y));
        h = hash64(h ^ static_cast<std::uint64_t>(iz+z));
        return 2.0 * static_cast<double>(h >> 11U) * 0x1.0p-53 - 1.0;
    };
    return mix(mix(mix(at(0,0,0), at(1,0,0), u), mix(at(0,1,0), at(1,1,0), u), v),
               mix(mix(at(0,0,1), at(1,0,1), u), mix(at(0,1,1), at(1,1,1), u), v), w);
}
double Noise::fbm(Vec3 p, int octaves) const {
    double result = 0, amplitude = 1, total = 0;
    for (int i = 0; i < octaves; ++i) {
        result += amplitude * value(p);
        total += amplitude;
        p = p*2.03 + Vec3{17.1, 9.2, 13.7};
        amplitude *= 0.5;
    }
    return total > 0 ? result / total : 0;
}
bool Modules::requested(Module m) const {
    const auto index = static_cast<unsigned>(m);
    return index < active_.size() && active_[index];
}
bool Modules::enabled(Module m) const {
    if (!requested(m)) return false;
    if (m == Module::Climate) return requested(Module::Atmosphere);
    if (m == Module::Biomes) return enabled(Module::Climate);
    if (m == Module::SurfaceDetail) return enabled(Module::Terrain);
    if (m == Module::Wind) return enabled(Module::Atmosphere);
    if (m == Module::Clouds) return enabled(Module::Climate) && enabled(Module::Wind);
    return true;
}
void Modules::set(Module m, bool enabled) {
    const auto index = static_cast<unsigned>(m);
    if (index < active_.size()) active_[index] = enabled;
}

Orbit orbit(const Config& c, double seconds) {
    const double meanAnomaly = tau * std::remainder(seconds, c.year) / c.year;
    double eccentricAnomaly = meanAnomaly;
    for (int i = 0; i < 8; ++i)
        eccentricAnomaly -= (eccentricAnomaly-c.eccentricity*std::sin(eccentricAnomaly)-meanAnomaly)
                          / (1-c.eccentricity*std::cos(eccentricAnomaly));
    const double trueAnomaly = std::atan2(std::sqrt(1-c.eccentricity*c.eccentricity)*std::sin(eccentricAnomaly),
                                         std::cos(eccentricAnomaly)-c.eccentricity);
    const double solarLongitude = trueAnomaly + radians(-77.1);
    const double declination = std::asin(std::sin(c.obliquity)*std::sin(solarLongitude));
    const double ascension = std::atan2(std::cos(c.obliquity)*std::sin(solarLongitude), std::cos(solarLongitude));
    // Sidereal rotation = solar rotation + orbital mean rotation.
    const double spin = tau * (std::remainder(seconds, c.day)/c.day + std::remainder(seconds, c.year)/c.year);
    const double distanceAU = 1-c.eccentricity*std::cos(eccentricAnomaly);
    return {direction(declination, ascension-spin), distanceAU, c.solarConstant/(distanceAU*distanceAU), declination};
}
double dailyInsolation(const Config& c, double lat, double seconds) {
    const auto sun = orbit(c, seconds);
    const double a = std::sin(lat)*std::sin(sun.declination);
    const double b = std::cos(lat)*std::cos(sun.declination);
    double hourAngle;
    if (b < 1e-14) hourAngle = a > 0 ? pi : 0;
    else hourAngle = std::acos(clamp(-a/b, -1.0, 1.0));
    return std::max(0.0, sun.irradiance/pi*(hourAngle*a + b*std::sin(hourAngle)));
}
Atmosphere standardAtmosphere(double altitude, double seaTemperature, double surfacePressure, double radius, double gravity) {
    if (!std::isfinite(altitude) || altitude < -10000 || altitude > 1e7 ||
        !std::isfinite(seaTemperature) || seaTemperature < 150 || seaTemperature > 400 ||
        !std::isfinite(surfacePressure) || surfacePressure <= 0 ||
        !std::isfinite(radius) || radius < 100000 || !std::isfinite(gravity) || gravity <= 0)
        throw std::invalid_argument("Atmosphere parameters are outside the supported range");
    const double h = radius*altitude/(radius+altitude);
    constexpr std::array<double, 8> boundaries{0,11000,20000,32000,47000,51000,71000,84852};
    constexpr std::array<double, 7> lapse{-0.0065,0,0.001,0.0028,0,-0.0028,-0.002};
    double temperature = seaTemperature, pressure = surfacePressure;
    const auto integrate = [&](double dh, double rate) {
        const double next = temperature + rate*dh;
        if (rate == 0) pressure *= std::exp(-gravity*dh/(gasConstantAir*temperature));
        else pressure *= std::pow(next/temperature, -gravity/(gasConstantAir*rate));
        temperature = next;
    };
    if (h < 0) integrate(h, lapse[0]);
    else {
        for (std::size_t i=0; i<lapse.size(); ++i) {
            if (h <= boundaries[i]) break;
            integrate(std::min(h, boundaries[i+1])-boundaries[i], lapse[i]);
        }
        // Above the standard-atmosphere table: only a display extrapolation.
        if (h > boundaries.back()) integrate(h-boundaries.back(), 0);
    }
    return {temperature, pressure, pressure/(gasConstantAir*temperature)};
}

std::string_view biomeName(Biome b) {
    constexpr std::array<std::string_view, 8> names{"Ocean", "Ice", "Tundra", "Desert", "Grassland", "Forest", "Rainforest", "Rock"};
    return names.at(static_cast<std::size_t>(b));
}
World::World(Config config) : config_(config), noise_(config.seed) {
    if (!std::isfinite(config.radius) || config.radius < 100000 || config.radius > 1e8 ||
        !std::isfinite(config.gravity) || config.gravity <= 0 || config.gravity > 100 ||
        !std::isfinite(config.solarConstant) || config.solarConstant <= 0 || config.solarConstant > 3000 ||
        !std::isfinite(config.obliquity) || config.obliquity < 0 || config.obliquity > pi/2 ||
        !std::isfinite(config.eccentricity) || config.eccentricity < 0 || config.eccentricity > 0.2 ||
        !std::isfinite(config.day) || config.day < 60 || !std::isfinite(config.year) || config.year < config.day ||
        !std::isfinite(config.seaLevel) || std::abs(config.seaLevel)>5000 ||
        !std::isfinite(config.emissivity) || config.emissivity < 0.3 || config.emissivity > 1 ||
        !std::isfinite(config.albedo) || config.albedo < 0 || config.albedo > 0.8)
        throw std::invalid_argument("Planet configuration is outside the supported terrestrial range");
    for (std::size_t i=0; i<plates_.size(); ++i) {
        const auto s = config.seed + static_cast<std::uint64_t>(i)*7;
        const double y = 2*randomUnit(s)-1;
        const double azimuth = tau*randomUnit(s+1);
        const Vec3 center{std::sqrt(1-y*y)*std::cos(azimuth), y, std::sqrt(1-y*y)*std::sin(azimuth)};
        const double angle = tau*randomUnit(s+2);
        plates_[i] = {center, east(center)*std::cos(angle)+north(center)*std::sin(angle), randomUnit(s+3)};
    }
    for (std::size_t i=0; i<annualSolar_.size(); ++i) {
        double sum = 0;
        for (int day=0; day<96; ++day)
            sum += dailyInsolation(config_, radians(static_cast<double>(i)-90), (day+0.5)*config_.year/96);
        annualSolar_[i] = sum/96;
    }
}
double World::elevation(Vec3 p) const {
    if (!modules_.enabled(Module::Terrain)) return 0;
    p = normalized(p);
    double best = -2;
    std::array<double,24> similarities{};
    for (std::size_t i=0; i<plates_.size(); ++i) {
        similarities[i] = dot(p, plates_[i].center);
        best = std::max(best,similarities[i]);
    }
    double weight=0, squaredWeight=0, continental=0;
    Vec3 meanCenter{},meanVelocity{};
    for (std::size_t i=0; i<plates_.size(); ++i) {
        const double w = std::exp(48*(similarities[i]-best));
        weight+=w; squaredWeight+=w*w; continental+=w*plates_[i].continental;
        meanCenter+=plates_[i].center*w; meanVelocity+=plates_[i].velocity*w;
    }
    const double blendedPlate = continental/weight;
    const double continent = noise_.fbm(p*2.7,5) + (blendedPlate-0.52)*0.55;
    const double land = smoothstep(-0.12,0.19,continent);
    // Smooth plate mixtures avoid cracks at Voronoi triple junctions.
    const double convergence = clamp(0.45+10*dot(meanVelocity/weight,meanCenter/weight),0.0,1.0);
    const double boundary = clamp(2*(1-squaredWeight/(weight*weight)),0.0,1.0);
    const double ridge = 1-std::abs(noise_.fbm(p*110,4));
    const double mountains = boundary * convergence * 6400 * ridge * ridge * land;
    const double rolling = noise_.fbm(p*900,5) * (30 + 140*land);
    // Metric wavelengths fill the gap between continental terrain and walking scale.
    // These are continuous geometric approximations, not an erosion simulation.
    const Vec3 metres = p*config_.radius;
    const double hills = noise_.fbm(metres/620.0,3)*(12+24*land);
    const double detail = noise_.fbm(metres/150.0,3)*7.0
                        + noise_.value(metres/32.0)*1.1
                        + noise_.value(metres/9.0)*0.22;
    return mix(-4800.0,550.0,land) + mountains + rolling + hills + detail;
}
double World::surface(Vec3 p) const {
    const double h = elevation(p);
    return modules_.enabled(Module::Ocean) ? std::max(h, config_.seaLevel) : h;
}
double World::annualInsolation(double lat) const {
    const double index = clamp(degrees(lat)+90.0,0.0,180.0);
    const auto lower = static_cast<std::size_t>(std::floor(index));
    return mix(annualSolar_[lower], annualSolar_[std::min(lower+1,annualSolar_.size()-1)], index-static_cast<double>(lower));
}
Sample World::sample(Vec3 p, double seconds) const {
    return evaluate(p,seconds,false);
}
Sample World::climatology(Vec3 p) const {
    return evaluate(p,0,true);
}
Sample World::evaluate(Vec3 p, double seconds, bool annualMean) const {
    p = normalized(p);
    Sample s;
    s.elevation = elevation(p);
    s.ocean = modules_.enabled(Module::Ocean) && s.elevation < config_.seaLevel;
    const double height = s.ocean ? config_.seaLevel : s.elevation;
    const double lat = latitude(p);
    const double annual = annualInsolation(lat);
    const auto sun = orbit(config_,seconds);
    s.insolation = annualMean ? annual : sun.irradiance * std::max(0.0,dot(p,sun.sunDirection));
    double seaTemperature = 288.15;
    if (modules_.enabled(Module::Climate)) {
        const double transported = 0.65*annual + 0.35*config_.solarConstant/4;
        const double equilibrium = std::pow(transported*(1-config_.albedo)/(stefanBoltzmann*config_.emissivity),0.25);
        const double feedback = 4*stefanBoltzmann*config_.emissivity*std::pow(equilibrium,3);
        const double heatCapacity = s.ocean ? 4.18e6*30 : 2e6*2;
        const double omega = tau/config_.year;
        const double lag = std::atan(omega*heatCapacity/feedback)/omega;
        const double damping = feedback/std::hypot(feedback,omega*heatCapacity);
        const double anomaly = (dailyInsolation(config_,lat,seconds-lag)-annual)*(1-config_.albedo)*damping/feedback;
        seaTemperature = clamp(equilibrium + (annualMean ? 0 : anomaly), 180.0, 340.0);
    }
    if (modules_.enabled(Module::Atmosphere)) {
        const auto air = standardAtmosphere(height, seaTemperature, 101325, config_.radius, config_.gravity);
        s.temperature = air.temperature; s.pressure = air.pressure; s.density = air.density;
    } else {
        // Air temperature and pressure are undefined/zero in a vacuum.
        s.temperature = std::numeric_limits<double>::quiet_NaN();
    }
    if (modules_.enabled(Module::Climate)) {
        const double latDegrees = std::abs(degrees(lat));
        const double tropical = 2100*std::exp(-std::pow(latDegrees/16,2));
        const double westerlies = 1000*std::exp(-std::pow((latDegrees-52)/17,2));
        s.precipitation = std::max(0.0, (120+tropical+westerlies)*(0.75+0.5*noise_.value(p*18)));
    }
    if (s.ocean) s.biome = Biome::Ocean;
    else if (modules_.enabled(Module::Biomes)) {
        if (s.temperature < 263.15) s.biome = Biome::Ice;
        else if (s.temperature < 277.15) s.biome = Biome::Tundra;
        else if (s.precipitation < 280) s.biome = Biome::Desert;
        else if (s.precipitation < 800) s.biome = Biome::Grassland;
        else if (s.temperature > 295 && s.precipitation > 1700) s.biome = Biome::Rainforest;
        else s.biome = Biome::Forest;
    }
    return s;
}
Color World::color(const Sample& s) const {
    switch(s.biome) {
        case Biome::Ocean: return {0.035f,0.20f,0.30f};
        case Biome::Ice: return {0.80f,0.87f,0.85f};
        case Biome::Tundra: return {0.37f,0.40f,0.31f};
        case Biome::Desert: return {0.66f,0.52f,0.32f};
        case Biome::Grassland: return {0.40f,0.45f,0.24f};
        case Biome::Forest: return {0.20f,0.33f,0.22f};
        case Biome::Rainforest: return {0.13f,0.29f,0.20f};
        default: return {0.46f,0.43f,0.38f};
    }
}
Vec3 World::findLand() const {
    Vec3 best{1,0,0};
    double bestScore = -1e20;
    for (int i=0; i<1500; ++i) {
        const double y = 1 - 2*(i+0.5)/1500;
        const Vec3 p = direction(std::asin(y), i*pi*(3-std::sqrt(5.0)));
        const double h = elevation(p);
        if (h > config_.seaLevel+10 && h < config_.seaLevel+1800) {
            const double score = -std::abs(h-config_.seaLevel-220)-600*std::abs(y-0.35);
            if (score > bestScore) { bestScore = score; best = p; }
        }
    }
    return best;
}
} // namespace planet

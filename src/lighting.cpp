#include "planet/lighting.hpp"
#include <stdexcept>

namespace planet {
namespace {
Color blend(Color a,Color b,double t) {return {static_cast<float>(mix(a.r,b.r,t)),static_cast<float>(mix(a.g,b.g,t)),static_cast<float>(mix(a.b,b.b,t))};}
Color scale(Color a,double t) {return {static_cast<float>(a.r*t),static_cast<float>(a.g*t),static_cast<float>(a.b*t)};}
}
double siderealAngle(const Config& c,double seconds) {
    if(!std::isfinite(seconds) || !(c.day>0) || !(c.year>0)) throw std::invalid_argument("Invalid astronomical clock");
    return tau*(std::remainder(seconds,c.day)/c.day+std::remainder(seconds,c.year)/c.year);
}
Vec3 inertialToPlanet(Vec3 p,const Config& c,double seconds) {return rotate(normalized(p),{0,1,0},-siderealAngle(c,seconds));}
double relativeAirMass(double elevation) {
    if(!std::isfinite(elevation)) throw std::invalid_argument("Invalid solar elevation");
    const double h=clamp(degrees(elevation),0.0,90.0);
    return 1/(std::sin(radians(h))+.50572*std::pow(h+6.07995,-1.6364));
}
Lighting lighting(const Config& c,Vec3 observer,double seconds,const SkyConditions& conditions) {
    if(!std::isfinite(length(observer)) || length(observer)<1e-9 || !std::isfinite(seconds) ||
       !std::isfinite(conditions.altitude) || !std::isfinite(conditions.cloudWater) || conditions.cloudWater<0 ||
       !std::isfinite(conditions.sunCloudWater) || conditions.sunCloudWater<0 || !std::isfinite(conditions.rainMmH) || conditions.rainMmH<0)
        throw std::invalid_argument("Invalid sky conditions");
    const auto sun=orbit(c,seconds);Lighting l;l.sun=sun.sunDirection;l.up=normalized(observer);
    l.elevation=std::asin(clamp(dot(l.up,l.sun),-1.0,1.0));
    l.horizon=-std::acos(c.radius/(c.radius+std::max(0.0,conditions.altitude)));
    l.angularRadius=.00465047/sun.distanceAU;
    l.airDensity=conditions.atmosphere ? std::exp(-std::max(0.0,conditions.altitude)/8500) : 0;
    const double e=degrees(l.elevation),q=clamp((l.elevation-l.horizon)/l.angularRadius,-1.0,1.0);
    const double disc=(std::asin(q)+q*std::sqrt(std::max(0.0,1-q*q))+pi/2)/pi;
    l.cloudCover=conditions.atmosphere ? 1-std::exp(-7*conditions.cloudWater) : 0;
    const double cloudTransmission=conditions.atmosphere ? std::exp(-7*conditions.sunCloudWater/std::max(.20,std::sin(std::max(0.0,l.elevation)))) : 1;
    const double air=relativeAirMass(l.elevation)*l.airDensity;
    const double solar=disc*cloudTransmission/(sun.distanceAU*sun.distanceAU);
    l.sunlight={static_cast<float>(solar*std::exp(-.065*air)),static_cast<float>(solar*std::exp(-.12*air)),static_cast<float>(solar*std::exp(-.26*air))};
    l.direct=(l.sunlight.r+l.sunlight.g+l.sunlight.b)/3;
    l.daylight=smoothstep(-12.0,12.0,e);
    l.twilight=smoothstep(-18.0,-3.0,e)*(1-smoothstep(1.0,12.0,e))*l.airDensity;
    l.night=1-smoothstep(-18.0,-6.0,e);
    const double clearStars=(1-l.cloudCover)*(1-l.cloudCover);
    // Daylight glare persists on mountains; only a nearly absent air column reveals daytime stars.
    const double spaceVisibility=1-smoothstep(0.0,.02,l.airDensity);
    l.starVisibility=mix(spaceVisibility,1.0,l.night)*clearStars;
    l.starlightLux=.0002*clearStars; // Representative clear moonless starlight, not a calibrated display.
    const double nightLight=l.night*(.20+.80*clearStars);
    // Display-adapted night fill: much brighter than raw stellar photometry, but weak beside daylight.
    l.ambient={static_cast<float>(.020*nightLight+.15*l.daylight*(1-.40*l.cloudCover)),
        static_cast<float>(.035*nightLight+.19*l.daylight*(1-.40*l.cloudCover)),
        static_cast<float>(.065*nightLight+.25*l.daylight*(1-.40*l.cloudCover))};
    if(!conditions.atmosphere) l.ambient={.006f,.010f,.018f};
    const Color zenithDay=blend({.055f,.24f,.54f},{.27f,.30f,.34f},l.cloudCover);
    const Color horizonDay=blend({.53f,.67f,.78f},{.31f,.34f,.38f},l.cloudCover);
    l.zenith=blend({.0015f,.003f,.010f},zenithDay,l.daylight);
    l.horizonColor=blend({.006f,.010f,.022f},horizonDay,l.daylight);
    const double storm=1-.25*clamp(conditions.rainMmH/60,0.0,1.0);
    l.zenith=scale(l.zenith,l.airDensity*storm);l.horizonColor=scale(l.horizonColor,l.airDensity*storm);
    l.cloudColor=blend({.008f,.012f,.022f},blend({.86f,.89f,.94f},{.30f,.33f,.37f},l.cloudCover),l.daylight);
    l.cloudColor=blend(l.cloudColor,{.55f,.24f,.14f},l.twilight*.35*(1-l.cloudCover));
    return l;
}
Color skyColor(const Lighting& l,Vec3 view) {
    view=normalized(view);const double height=dot(view,l.up),vertical=std::pow(clamp(height,0.0,1.0),.45);
    Color color=blend(l.horizonColor,l.zenith,vertical);
    const auto solarAzimuth=normalized(l.sun-l.up*dot(l.sun,l.up));
    const auto lookAzimuth=normalized(view-l.up*height);
    const double toward=std::pow(std::max(0.0,dot(solarAzimuth,lookAzimuth)),6);
    const double belt=std::exp(-std::abs(height)*7)*l.twilight*(1-.8*l.cloudCover);
    color=blend(color,{.67f,.19f,.045f},belt*toward*.85);
    color=blend(color,{.13f,.075f,.19f},belt*(1-toward)*.3);
    const double glow=std::pow(std::max(0.0,dot(view,l.sun)),48)*l.direct*l.airDensity*.16;
    return {static_cast<float>(clamp(color.r+glow,0.0,1.0)),static_cast<float>(clamp(color.g+glow*.75,0.0,1.0)),static_cast<float>(clamp(color.b+glow*.45,0.0,1.0))};
}
std::vector<Star> starCatalog(std::uint64_t seed,unsigned count) {
    if(count>20000) throw std::invalid_argument("Star catalog is bounded to 20000 stars");
    std::vector<Star> stars;stars.reserve(count);
    for(unsigned i=0;i<count;++i) {
        const auto id=hash64(seed^hash64(i+710235));
        const auto p=direction(std::asin(2*randomUnit(id)-1),tau*randomUnit(id+1));
        const double magnitude=i<24 ? -.8+3*randomUnit(id+2) : 1.5+5*std::pow(randomUnit(id+2),.32);
        const double tint=randomUnit(id+3);
        const Color color=tint<.30 ? blend({1,.55f,.31f},{1,.95f,.80f},tint/.30) : blend({1,.95f,.80f},{.58f,.72f,1},(tint-.30)/.70);
        stars.push_back({p,magnitude,color});
    }
    return stars;
}
double solarHourTime(const Config& c,Vec3 observer,double reference,double hour) {
    if(!std::isfinite(hour) || hour<0 || hour>24 || !std::isfinite(reference)) throw std::invalid_argument("Solar hour must be 0..24");
    double time=std::floor(reference/c.day)*c.day+hour*c.day/24;
    for(int i=0;i<5;++i) {
        const double angle=std::remainder(longitude(observer)-longitude(orbit(c,time).sunDirection),tau);
        time+=std::remainder((hour-12)*tau/24-angle,tau)*c.day/tau;
    }
    return time;
}
} // namespace planet

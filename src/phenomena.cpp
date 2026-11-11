#include "planet/phenomena.hpp"
#include "planet/hydrology.hpp"
#include <stdexcept>

namespace planet {
std::string_view phenomenonName(Phenomenon k) {
    constexpr std::array<std::string_view,8> names{"natural","rainbow","halo","pillar","aurora","lightning","fog","dust"};
    return names.at(static_cast<std::size_t>(k));
}
std::optional<Phenomenon> phenomenonType(std::string_view name) {
    for(unsigned i=0;i<8;++i) if(phenomenonName(static_cast<Phenomenon>(i))==name) return static_cast<Phenomenon>(i);
    return {};
}
double dewPoint(double temperature,double humidity) {
    if(!std::isfinite(temperature) || temperature<150 || temperature>400 || !std::isfinite(humidity) || humidity<0 || humidity>2)
        throw std::invalid_argument("Invalid dew-point conditions");
    const double c=temperature-273.15,gamma=std::log(std::max(1e-6,humidity))+17.625*c/(243.04+c);
    return 273.15+243.04*gamma/(17.625-gamma);
}
double primaryRainbowRadius(double n) {
    if(!std::isfinite(n) || n<=1 || n>=2) throw std::invalid_argument("Invalid water refractive index");
    const double i=std::acos(std::sqrt((n*n-1)/3)),r=std::asin(std::sin(i)/n);
    return 4*r-2*i;
}
double haloRadius(double n) {
    if(!std::isfinite(n) || n<=1 || n>=2) throw std::invalid_argument("Invalid ice refractive index");
    return 2*std::asin(n*.5)-pi/3;
}
Vec3 magneticPole(std::uint64_t seed) {return direction(radians(81),tau*randomUnit(seed^0x619ec1));}
double magneticLatitude(Vec3 p,std::uint64_t seed) {return std::asin(clamp(dot(normalized(p),magneticPole(seed)),-1.,1.));}
PhenomenaForcing phenomenaForcing(const World& world,const Simulation& sim,Vec3 p,double altitude,double seconds,Phenomenon experiment,double experimentSeconds) {
    PhenomenaForcing f;f.enabled=world.modules().enabled(Module::Phenomena);if(!f.enabled) return f;
    p=normalized(p);const auto ground=world.sample(p,seconds);const auto air=sim.weather.sample(p);const auto global=sim.global.sample(p);
    const auto snow=sim.water.snow.sample(p);const auto soil=sim.water.sample(p);
    f.clouds=world.modules().enabled(Module::Clouds);f.wind=world.modules().enabled(Module::Wind);
    const auto sun=orbit(world.config(),seconds).sunDirection;
    const double solarHeight=degrees(std::asin(clamp(dot(p,sun),-1.,1.)));
    f.aboveGround=std::max(0.,altitude-world.surface(p));
    f.temperature=std::isfinite(ground.temperature) ? ground.temperature : 288.15;
    // Convert the cloud-layer mixing ratio to surface RH before applying the
    // shallow nocturnal cooling approximation. Saturated cloud air is not
    // automatically saturated at the warmer ground below it.
    const double columnTemperature=air.available ? air.temperature : global.temperature;
    const double columnPressure=air.available ? air.pressure : global.pressure;
    const double columnHumidity=air.available ? air.humidity : global.humidity;
    f.humidity=f.clouds ? columnHumidity*saturationMixingRatio(columnTemperature,columnPressure)/saturationMixingRatio(f.temperature,ground.pressure) : 0;
    f.velocity=f.wind ? air.available ? air.wind : Wind2{dot(global.velocity,east(p)),dot(global.velocity,north(p))} : Wind2{};
    f.windSpeed=std::hypot(f.velocity.east,f.velocity.north);
    f.wetness=clamp(std::max({ground.wetness,ground.ocean || ground.freshwater ? 1. : 0.,soil.available ? soil.saturation : 0.}),0.,1.);
    f.cloudWater=f.clouds ? air.available ? air.cloudWater : global.cloud : 0;
    // Boundary-layer humidity/cooling are parameterized from the resolved column.
    // No extra water is inserted into the conservative weather/hydrology reservoirs.
    const double cooling=(1-smoothstep(-5.,10.,solarHeight))*(1-smoothstep(1.,8.,f.windSpeed))*std::exp(-6*f.cloudWater)*3.5;
    f.humidity=clamp((f.humidity+.04*f.wetness*(1-smoothstep(1.,8.,f.windSpeed)))*saturationPressure(f.temperature)/saturationPressure(f.temperature-cooling),0.,1.2);
    f.temperature-=cooling;
    f.snowDepth=snow.available ? snow.depth : referenceSnowDepth(ground);
    const auto away=normalized(-sun-p*dot(-sun,p));
    const auto shower=sim.weather.sample(normalized(p+away*(2500/world.config().radius)));
    const double rain=f.clouds ? std::max(air.rainRate,shower.rainRate)*3600+sim.rainExperiment() : 0;
    f.rainMmH=rain*(1-snowFraction(f.temperature,clamp(f.humidity,0.,1.)));
    const auto toward=normalized(sun-p*dot(sun,p));const auto sunAir=sim.weather.sample(normalized(p+toward*(2500/world.config().radius)));
    f.sunCloudWater=f.clouds ? sunAir.available ? sunAir.cloudWater : global.cloud : 0;
    const double upperTemperature=f.temperature-.0065*std::max(0.,6500-ground.elevation);
    f.upperIce=(1-smoothstep(238.,268.,upperTemperature))*smoothstep(.003,.10,std::max(global.cloud*.35,f.cloudWater*.25));
    f.lowIce=(1-smoothstep(253.15,266.15,f.temperature))*smoothstep(.75,1.,f.humidity);
    const double barren=ground.biome==Biome::Desert || ground.biome==Biome::Rock ? 1. : ground.biome==Biome::Grassland ? .35 : 0;
    f.dustSource=barren*(1-smoothstep(280.,900.,ground.precipitation))*(1-f.wetness)*(ground.ocean || ground.freshwater ? 0 : 1);
    f.geomagneticActivity=clamp(.28+.38*Noise(world.config().seed^0xada91).value({seconds/18000,0,0})+.32*std::pow(.5+.5*std::sin(seconds/(27*86400)*tau),6),0.,1.);
    if(world.modules().enabled(Module::GlobalWeather) && f.clouds) for(const auto& strike:sim.global.lightning()) {
        const double age=sim.global.effectsTime()-strike.time,range=length(strike.direction-p)*world.config().radius;
        if(age<0 || age>.35 || range<30000 || range>300000) continue;
        const auto ray=normalized(strike.direction*(world.config().radius+9000)-p*(world.config().radius+altitude));
        if(dot(ray,p)<0) continue;
        f.flashes.push_back({ray,strike.strength*(1-age/.35)*std::exp(-range/180000)});
        if(f.flashes.size()==32) break;
    }
    if(experiment!=Phenomenon::Natural) {
        f.rainMmH=0;f.upperIce=0;f.lowIce=0;f.cloudWater=.01;f.sunCloudWater=.005;f.flashes.clear();
        f.wetness=0;f.dustSource=0;f.humidity=.55;f.windSpeed=2;f.temperature=288.15;f.snowDepth=0;
        if(experiment==Phenomenon::Rainbow) {f.rainMmH=8;f.humidity=.75;}
        if(experiment==Phenomenon::Halo) f.upperIce=1;
        if(experiment==Phenomenon::Pillar) {f.temperature=253.15;f.lowIce=1;f.humidity=.9;f.windSpeed=1;}
        if(experiment==Phenomenon::Aurora) {f.cloudWater=0;f.geomagneticActivity=.8;}
        if(experiment==Phenomenon::Fog) {f.humidity=1;f.wetness=1;f.temperature=278.15;f.windSpeed=.8;}
        if(experiment==Phenomenon::Dust) {f.dustSource=1;f.temperature=304.15;f.humidity=.14;f.windSpeed=24;f.velocity={20,13};}
        if(experiment==Phenomenon::DistantLightning && world.modules().enabled(Module::GlobalWeather)) {
            const double phase=std::fmod(std::max(0.,experimentSeconds),6.);
            if(phase<.45) f.flashes.push_back({normalized(north(p)+p*.07),.9*(1-phase/.45)});
        }
    }
    return f;
}
PhenomenaState atmosphericPhenomena(const Config& config,Vec3 p,double seconds,const PhenomenaForcing& f) {
    PhenomenaState s;if(!f.enabled) return s;
    for(double value:{f.temperature,f.humidity,f.windSpeed,f.rainMmH,f.cloudWater,f.sunCloudWater,f.upperIce,f.lowIce,f.wetness,f.dustSource,f.snowDepth,f.aboveGround,f.geomagneticActivity})
        if(!std::isfinite(value) || value<0) throw std::invalid_argument("Invalid atmospheric phenomenon forcing");
    if(f.humidity>2 || !std::isfinite(seconds) || !std::isfinite(length(p)) || length(p)<.5) throw std::invalid_argument("Invalid phenomenon observer or clock");
    p=normalized(p);s.enabled=true;s.dewPointK=dewPoint(f.temperature,f.humidity);s.magneticLatitude=magneticLatitude(p,config.seed);s.activity=clamp(f.geomagneticActivity,0.,1.);
    const double elevation=degrees(std::asin(clamp(dot(orbit(config,seconds).sunDirection,p),-1.,1.)));
    const double sunlight=smoothstep(-.5,5.,elevation)*std::exp(-7*f.sunCloudWater/std::max(.2,std::sin(radians(std::max(0.,elevation)))));
    const double clear=std::exp(-7*f.cloudWater),low=std::exp(-f.aboveGround/90);
    if(f.clouds) {
        s.rainbow=sunlight*smoothstep(.1,3.,f.rainMmH)*(1-smoothstep(38.,43.,elevation))*smoothstep(271.,276.,f.temperature);
        s.halo=sunlight*clamp(f.upperIce,0.,1.)*clear;
        s.pillar=smoothstep(-6.,-1.,elevation)*(1-smoothstep(5.,14.,elevation))*clamp(f.lowIce,0.,1.)*(1-smoothstep(2.,7.,f.windSpeed))*clear*low;
        s.fog=smoothstep(.94,1.,f.humidity)*(1-smoothstep(3.,12.,f.windSpeed))*std::max(clamp(f.wetness,0.,1.),1-smoothstep(-3.,12.,elevation))*low;
        if(f.flashes.size()>32) throw std::invalid_argument("Too many distant lightning flashes");
        for(const auto& flash:f.flashes) {
            if(!std::isfinite(flash.intensity) || flash.intensity<0 || !std::isfinite(length(flash.direction)) || length(flash.direction)<.5) throw std::invalid_argument("Invalid distant lightning");
            s.flashes.push_back({normalized(flash.direction),clamp(flash.intensity*(1-smoothstep(-5.,15.,elevation)*.85)*std::exp(-3*f.cloudWater),0.,1.)});
        }
    }
    const double oval=70-8*s.activity,latitude=std::abs(degrees(s.magneticLatitude));
    s.aurora=std::exp(-.5*std::pow((latitude-oval)/(4+3*s.activity),2))*s.activity*(1-smoothstep(-14.,-5.,elevation))*clear*clear;
    if(f.wind) s.dust=clamp(f.dustSource,0.,1.)*smoothstep(8.,22.,f.windSpeed)*(1-smoothstep(.4,.8,f.humidity))*(1-smoothstep(.1,2.,f.rainMmH))*(1-smoothstep(.001,.03,f.snowDepth))*std::exp(-f.aboveGround/400);
    const double aerosolTransmission=std::exp(-4*(s.fog+s.dust));
    s.rainbow*=aerosolTransmission;s.halo*=aerosolTransmission;s.pillar*=aerosolTransmission;s.aurora*=aerosolTransmission;
    for(auto& flash:s.flashes) flash.intensity*=aerosolTransmission;
    const double extinction=s.fog/80+s.dust/180;
    s.visibility=extinction>0 ? std::min(100000.,1/extinction) : 100000;
    const double dustWeight=s.dust/(s.fog+s.dust+1e-9);
    s.haze={static_cast<float>(mix(.68,.62,dustWeight)),static_cast<float>(mix(.73,.44,dustWeight)),static_cast<float>(mix(.76,.24,dustWeight))};
    return s;
}
std::vector<OpticalVertex> atmosphericSky(const Config& config,Vec3 p,double altitude,double seconds,double animation,const PhenomenaState& s) {
    std::vector<OpticalVertex> result;if(!s.enabled) return result;p=normalized(p);
    const double horizon=-std::sqrt(std::max(0.,1-std::pow(config.radius/(config.radius+std::max(0.,altitude)),2)));
    const auto vertex=[&](Vec3 d,Color c,double alpha){d=normalized(d);return OpticalVertex{d,c,static_cast<float>(clamp(alpha*smoothstep(horizon,horizon+.015,dot(d,p)),0.,1.))};};
    const auto triangle=[&](OpticalVertex a,OpticalVertex b,OpticalVertex c){if(a.alpha+b.alpha+c.alpha>.001f) {result.push_back(a);result.push_back(b);result.push_back(c);}};
    const auto sun=orbit(config,seconds).sunDirection;
    constexpr std::array<Color,7> spectrum{{{1,.20f,.12f},{1,.55f,.12f},{1,.90f,.24f},{.27f,.85f,.25f},{.19f,.60f,1},{.30f,.28f,.88f},{.58f,.23f,.85f}}};
    const auto ring=[&](Vec3 axis,double intensity,bool rainbow) {
        if(intensity<.003) return;
        const auto a=east(axis),b=normalized(cross(axis,a));
        for(unsigned band=0;band<7;++band) {
            const auto angle=[&](double t){return rainbow ? primaryRainbowRadius(mix(1.331,1.343,t)) : haloRadius(mix(1.306,1.317,t));};
            const double inner=angle(band/7.),outer=angle((band+1)/7.);
            const auto point=[&](double t,double r){return axis*std::cos(r)+(a*std::cos(t)+b*std::sin(t))*std::sin(r);};
            for(int i=0;i<192;++i) {
                const double t=tau*i/192,u=tau*(i+1)/192,alpha=intensity*(rainbow ? .32 : .15);
                const auto v0=vertex(point(t,inner),spectrum[band],alpha),v1=vertex(point(u,inner),spectrum[band],alpha);
                const auto v2=vertex(point(t,outer),spectrum[band],alpha),v3=vertex(point(u,outer),spectrum[band],alpha);
                triangle(v0,v1,v2);triangle(v1,v3,v2);
            }
        }
    };
    ring(-sun,s.rainbow,true);ring(sun,s.halo,false);
    if(s.pillar>.003) {
        const double solarElevation=std::asin(clamp(dot(sun,p),-1.,1.));const auto axis=normalized(sun-p*dot(sun,p)),side=normalized(cross(axis,p));
        for(int row=0;row<32;++row) for(int col=0;col<8;++col) {
            const auto v=[&](int x,int y){const double h=solarElevation+radians(18)*y/32.,w=(x/4.-1)*radians(.8);const auto d=axis*(std::cos(h)*std::cos(w))+p*std::sin(h)+side*(std::cos(h)*std::sin(w));return vertex(d,{1,.72f,.38f},s.pillar*.55*std::exp(-y/10.)*std::pow(1-std::abs(x/4.-1),2));};
            triangle(v(col,row),v(col+1,row),v(col,row+1));triangle(v(col+1,row),v(col+1,row+1),v(col,row+1));
        }
    }
    if(s.aurora>.003) {
        const auto pole=magneticPole(config.seed)*(s.magneticLatitude<0 ? -1 : 1),a=east(pole),b=normalized(cross(pole,a));
        const auto v=[&](int i,int layer) {
            const double angle=tau*i/160,lat=radians(70-8*s.activity+1.8*std::sin(angle*9+animation*.06)+.8*std::sin(angle*23-animation*.11));
            const auto up=pole*std::sin(lat)+(a*std::cos(angle)+b*std::sin(angle))*std::cos(lat);
            const double t=layer/5.,height=95000+140000*t,brightness=(.65+.35*std::sin(angle*47+animation*.15))*std::sin(pi*t);
            const Color color=t<.55 ? Color{.15f,.85f,.40f} : Color{.70f,.18f,.34f};
            return vertex(up*(config.radius+height)-p*(config.radius+altitude),color,s.aurora*.8*brightness);
        };
        for(int i=0;i<160;++i) for(int layer=0;layer<5;++layer) {triangle(v(i,layer),v(i+1,layer),v(i,layer+1));triangle(v(i+1,layer),v(i+1,layer+1),v(i,layer+1));}
    }
    for(const auto& flash:s.flashes) if(flash.intensity>.003) {
        const auto a=east(flash.direction),b=normalized(cross(flash.direction,a));
        for(int i=0;i<64;++i) {
            const auto edge=[&](int j){return normalized(flash.direction+a*(.36*std::cos(tau*j/64))+b*(.15*std::sin(tau*j/64)));};
            triangle(vertex(flash.direction,{.74f,.80f,1},flash.intensity*.75),vertex(edge(i),{.60f,.66f,1},0),vertex(edge(i+1),{.60f,.66f,1},0));
        }
    }
    return result;
}
std::vector<DustParticle> dustParticles(const Config& c,Vec3 p,double ground,double seconds,Wind2 wind,double strength) {
    if(!std::isfinite(seconds) || !std::isfinite(ground) || !std::isfinite(wind.east) || !std::isfinite(wind.north) || !std::isfinite(strength) || strength<0 || strength>1)
        throw std::invalid_argument("Invalid dust conditions");
    std::vector<DustParticle> result;if(strength<.01) return result;
    p=normalized(p);const double lat=latitude(p),step=160/c.radius;
    const auto row=static_cast<std::int64_t>(std::floor(lat/step));const double parallel=std::max(.1,std::cos((static_cast<double>(row)+.5)*step));
    const auto column=static_cast<std::int64_t>(std::floor(longitude(p)*parallel/step));
    const auto wrap=[](double v){return v-160*std::floor(v/160);};
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) for(unsigned i=0;i<20;++i) {
        const auto id=hash64(c.seed^hash64(static_cast<std::uint64_t>(row+y))^hash64(static_cast<std::uint64_t>(column+x)+971)^hash64(i+31));
        const double z=(static_cast<double>(row+y)+wrap(randomUnit(id)*160+wind.north*seconds)/160)*step;
        const double q=(static_cast<double>(column+x)+wrap(randomUnit(id+1)*160+wind.east*seconds)/160)*step/parallel;
        const auto up=direction(clamp(z,-pi/2,pi/2),q);const double range=length(up-p)*c.radius;if(range>200) continue;
        const double h=1+35*randomUnit(id+2),fade=(1-smoothstep(110.,200.,range));
        result.push_back({up*(c.radius+ground+h),2+5*randomUnit(id+3),strength*.16*fade});
    }
    return result;
}
PhenomenonVisit findPhenomenon(const World& w,Phenomenon kind,double seconds) {
    if(!w.modules().enabled(Module::Phenomena)) throw std::invalid_argument("Enable Atmosphere and Atmospheric phenomena");
    if(kind!=Phenomenon::Natural && kind!=Phenomenon::Aurora && kind!=Phenomenon::Dust && !w.modules().enabled(Module::Clouds)) throw std::invalid_argument("This phenomenon requires Clouds");
    if(kind==Phenomenon::Dust && !w.modules().enabled(Module::Wind)) throw std::invalid_argument("Dust requires Wind");
    if(kind==Phenomenon::DistantLightning && !w.modules().enabled(Module::GlobalWeather)) throw std::invalid_argument("Distant lightning requires Global weather");
    Vec3 p=w.findLand();
    if(kind==Phenomenon::Pillar) p=findSnowfield(w,p,seconds);
    if(kind==Phenomenon::Fog && w.modules().enabled(Module::InlandWaters)) p=w.findLandmark(Landform::Wetland,p).position;
    if(kind==Phenomenon::Aurora || kind==Phenomenon::Dust) {
        double best=-1e30;
        for(int i=0;i<3072;++i) {
            const auto q=direction(std::asin(1-2*(i+.5)/3072),i*pi*(3-std::sqrt(5.)));const auto s=w.climatology(q);
            if(s.ocean || s.freshwater || s.elevation<10 || s.elevation>2500) continue;
            const double score=kind==Phenomenon::Aurora ? -std::abs(std::abs(degrees(magneticLatitude(q,w.config().seed)))-60) : -s.precipitation*.01+(s.biome==Biome::Desert ? 10 : 0);
            if(score>best) {best=score;p=q;}
        }
    }
    const double hour=kind==Phenomenon::Aurora || kind==Phenomenon::DistantLightning ? 0 : kind==Phenomenon::Pillar ? 6.6 : kind==Phenomenon::Fog ? 7 : kind==Phenomenon::Rainbow ? 8 : 10;
    const double target=kind==Phenomenon::Aurora || kind==Phenomenon::DistantLightning ? -25 : kind==Phenomenon::Pillar ? 2 : kind==Phenomenon::Rainbow ? 20 : kind==Phenomenon::Fog ? 4 : 40;
    const double reference=seconds;double best=1e30;
    // Rare-event previews choose a useful solar elevation, including during
    // polar day/night. This changes the demo calendar, never the natural model.
    for(int month=0;month<12;++month) {
        const double date=reference+month*w.config().year/12;
        for(int step=0;step<144;++step) {
            const double h=step/6.,t=solarHourTime(w.config(),p,date,h);
            const double elevation=degrees(std::asin(clamp(dot(p,orbit(w.config(),t).sunDirection),-1.,1.)));
            const double score=std::abs(elevation-target)+.001*std::abs(h-hour);
            if(score<best) {best=score;seconds=t;}
        }
        if(best<1.5) break;
    }
    const auto sun=orbit(w.config(),seconds).sunDirection;
    Vec3 look=kind==Phenomenon::Rainbow ? -sun : kind==Phenomenon::Halo || kind==Phenomenon::Pillar ? sun : north(p);
    if(kind==Phenomenon::Aurora) look=normalized(magneticPole(w.config().seed)*(magneticLatitude(p,w.config().seed)<0 ? -1 : 1)-p*dot(p,magneticPole(w.config().seed)*(magneticLatitude(p,w.config().seed)<0 ? -1 : 1)));
    look=normalized(look-p*dot(look,p));
    const double pitch=kind==Phenomenon::Halo ? std::asin(clamp(dot(sun,p),-1.,1.)) : kind==Phenomenon::Aurora ? radians(32) : kind==Phenomenon::Dust || kind==Phenomenon::Fog ? 0 : radians(12);
    return {p,normalized(look*std::cos(pitch)+p*std::sin(pitch)),seconds};
}
} // namespace planet

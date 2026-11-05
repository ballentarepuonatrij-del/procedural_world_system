#include "planet/water_motion.hpp"
#include "planet/hydrology.hpp"
#include "planet/rivers.hpp"
#include <complex>
#include <stdexcept>

namespace planet {
Wind2 windStress(Wind2 wind,Wind2 current) {
    const double x=wind.east-current.east,z=wind.north-current.north,speed=std::hypot(x,z);
    if(!std::isfinite(speed) || speed>100) throw std::invalid_argument("Invalid surface wind");
    const double drag=clamp((1.1+0.035*speed)*0.001,0.0011,0.003);
    return {1.225*drag*speed*x,1.225*drag*speed*z};
}
double waveFrequency(double k,double depth,double gravity) {
    if(!std::isfinite(k) || k<=0 || !std::isfinite(depth) || depth<=0 || !std::isfinite(gravity) || gravity<=0)
        throw std::invalid_argument("Invalid wave dispersion parameters");
    return std::sqrt(gravity*k*std::tanh(k*depth));
}
Wind2 advanceSlab(Wind2 u,Wind2 stress,double depth,double f,double drag,double seconds) {
    if(!std::isfinite(u.east) || !std::isfinite(u.north) || !std::isfinite(stress.east) || !std::isfinite(stress.north) ||
       !std::isfinite(depth) || depth<=0 || !std::isfinite(f) || !std::isfinite(drag) || drag<0 ||
       !std::isfinite(seconds) || seconds<0 || seconds>120) throw std::invalid_argument("Invalid ocean slab state");
    // Exact solution of du/dt = stress/(rho H) - (drag + i f) u.
    const std::complex<double> rate{drag,f},old{u.east,u.north},force{stress.east/(1025*depth),stress.north/(1025*depth)};
    const auto decay=std::exp(-rate*seconds);
    const auto next=std::abs(rate)<1e-12 ? old+force*seconds : old*decay+force*(1.0-decay)/rate;
    return {next.real(),next.imag()};
}
double waterPressure(double air,double depth,bool ocean,double gravity) {
    if(!std::isfinite(air) || air<0 || !std::isfinite(depth) || depth<0 || !std::isfinite(gravity) || gravity<=0)
        throw std::invalid_argument("Invalid hydrostatic pressure parameters");
    return air+(ocean ? 1025 : 1000)*gravity*depth;
}
WaterMotion::WaterMotion(const World& world,Vec3 observer) : config_(world.config()),anchor_(normalized(observer)),east_(east(anchor_)),north_(north(anchor_)) {}
void WaterMotion::setWindExperiment(std::optional<double> speed) {
    if(speed && (!std::isfinite(*speed) || *speed<0 || *speed>40)) throw std::invalid_argument("Water wind experiment must be in [0,40] m/s");
    experiment_=speed;
}
Vec3 WaterMotion::windAt(const Weather& weather,Vec3 p) const {
    if(!enabled_ || !weather.windEnabled()) return {};
    if(experiment_) return east(p)*(*experiment_);
    // The atmospheric model has one elevated layer. This prescribed reduction
    // is a surface-wind approximation, not a resolved boundary-layer profile.
    return weather.sample(p).velocity*0.7;
}
std::array<double,24> WaterMotion::targets(Vec3 wind) const {
    std::array<double,24> result{}; const double speed=length(wind);
    if(speed<0.1) return result;
    // Fetch-limited SMB growth curves; discrete directional variance spectrum.
    const double fetchStar=config_.gravity*fetch_/(speed*speed);
    const double hs=0.283*speed*speed/config_.gravity*std::tanh(0.0125*std::pow(fetchStar,0.42));
    const double period=7.54*speed/config_.gravity*std::tanh(0.077*std::pow(fetchStar,0.25));
    const double peak=config_.gravity*period*period/tau;
    double total=0;
    for(std::size_t i=0;i<waves_.size();++i) {
        const auto& wave=waves_[i];
        const double alignment=std::max(0.0,dot(wave.direction,wind)/speed);
        const double spectral=std::exp(-0.5*std::pow(std::log((tau/wave.k)/std::max(1.0,peak))/0.7,2));
        result[i]=std::pow(alignment,8)*spectral; total+=result[i];
    }
    for(auto& value:result) value=total>0 ? value/total*hs*hs/16 : 0;
    return result;
}
void WaterMotion::synchronize(const World& world,const Weather& weather,Vec3 observer) {
    if(world.config().seed!=config_.seed) throw std::invalid_argument("Reset water motion after changing the planet");
    enabled_=world.modules().enabled(Module::WaterMotion);
    globalFlow_=enabled_ && world.modules().enabled(Module::Ocean) && world.modules().enabled(Module::Wind) && !experiment_;
    if(globalFlow_) circulation_=world.waterNetwork();
    observer=normalized(observer);
    const bool moved=length(observer-anchor_)*config_.radius>5000;
    if(!initialized_ || moved) {
        anchor_=observer; east_=east(anchor_); north_=north(anchor_);
        const double depth=config_.seaLevel-world.elevation(anchor_);
        oceanHere_=world.modules().enabled(Module::Ocean) && depth>-30;
        mixedDepth_=clamp(depth,1.0,15.0); current_={};
        // Local incoming wave field at a representative depth. Shore amplitudes
        // are limited separately; no refraction or global wave transport is solved.
        const double referenceDepth=std::max(5.0,depth);
        fetch_=oceanHere_ ? 5000 : 40;
        const Vec3 forcing=windAt(weather,anchor_);
        if(oceanHere_ && length(forcing)>0.1) for(double distance=100;distance<=5000;distance+=100) {
            const auto p=normalized(anchor_-normalized(forcing)*(distance/config_.radius));
            if(world.elevation(p)>config_.seaLevel) { fetch_=distance; break; }
        }
        for(std::size_t i=0;i<waves_.size();++i) {
            const double angle=tau*static_cast<double>(i/3)/8;
            const double wavelength=std::array<double,3>{8,24,72}[i%3];
            waves_[i]={east_*std::cos(angle)+north_*std::sin(angle),tau/wavelength,
                waveFrequency(tau/wavelength,referenceDepth,config_.gravity),tau*randomUnit(config_.seed+8917+i),0};
        }
        const auto spectrum=targets(forcing);
        for(std::size_t i=0;i<waves_.size();++i) waves_[i].variance=spectrum[i];
        initialized_=true;
    }
    oceanHere_=world.modules().enabled(Module::Ocean) && config_.seaLevel-world.elevation(observer)>-30;
    wind_=windAt(weather,observer);
    if(probes_.empty()) probes_.resize(160);
    for(std::size_t i=0;i<probes_.size();++i) if(length(probes_[i]-observer)*config_.radius>250) {
        const auto id=hash64(config_.seed+77291+i);
        const double angle=tau*randomUnit(id),distance=200*std::sqrt(randomUnit(id+1));
        probes_[i]=normalized(observer+(east(observer)*std::cos(angle)+north(observer)*std::sin(angle))*(distance/config_.radius));
    }
}
void WaterMotion::advanceClock(double seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>86400) throw std::invalid_argument("Invalid wave clock step");
    if(enabled_) time_+=seconds;
}
void WaterMotion::step(const World& world,const Weather& weather,double seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>120) throw std::invalid_argument("Invalid water motion step");
    if(!enabled_) return;
    wind_=windAt(weather,anchor_);
    const Wind2 wind{dot(wind_,east_),dot(wind_,north_)};
    const auto stress=windStress(wind,current_);
    const double f=2*tau/config_.day*std::sin(latitude(anchor_));
    if(oceanHere_) current_=advanceSlab(current_,stress,mixedDepth_,f,1.0/14400+0.0025*std::hypot(current_.east,current_.north)/mixedDepth_,seconds);
    for(auto& p:probes_) p=normalized(p+current(p)*(seconds/config_.radius));
    const auto spectrum=targets(wind_);
    for(std::size_t i=0;i<waves_.size();++i) {
        auto& energy=waves_[i].variance;
        const double rate=1-std::exp(-seconds/(spectrum[i]>energy ? 180 : 900));
        energy=mix(energy,spectrum[i],rate);
    }
    (void)world;
}
WaveSurface WaterMotion::waves(Vec3 p,double depth,bool ocean,double minimumWavelength) const {
    if(!enabled_ || depth<=0 || !initialized_ || minimumWavelength>=72) return {};
    double bound=0; for(const auto& w:waves_) bound+=std::sqrt(2*w.variance)*(ocean ? 1.0 : (w.k>0.2 ? 0.2 : 0));
    if(bound<1e-12) return {};
    const double scale=std::min(1.0,0.35*depth/bound);
    WaveSurface result; const Vec3 point=(p-anchor_)*config_.radius;
    for(const auto& w:waves_) {
        if(w.variance<1e-18 || tau/w.k<=minimumWavelength) continue;
        const double resolved=minimumWavelength>0 ? smoothstep(minimumWavelength,minimumWavelength*2,tau/w.k) : 1;
        const double a=std::sqrt(2*w.variance)*scale*(ocean ? 1.0 : (w.k>0.2 ? 0.2 : 0))*resolved;
        const double phase=w.k*dot(point,w.direction)-w.omega*time_+w.phase;
        result.height+=a*std::cos(phase);
        result.gradient+=w.direction*(-a*w.k*std::sin(phase));
    }
    return result;
}
Vec3 WaterMotion::current(Vec3 p,double belowSurface) const {
    if(!enabled_ || !oceanHere_) return {};
    auto flow=(east_*current_.east+north_*current_.north)*std::exp(-std::max(0.0,belowSurface-mixedDepth_)/mixedDepth_);
    if(globalFlow_ && circulation_) flow+=circulation_->oceanCurrent(p,belowSurface);
    return flow-p*dot(flow,p);
}
double WaterMotion::significantHeight() const {
    double variance=0; if(enabled_) for(const auto& w:waves_) variance+=w.variance;
    return 4*std::sqrt(variance);
}
LiquidSample liquidAt(const World& world,const Hydrology* water,const WaterMotion* motion,Vec3 p) {
    p=normalized(p); LiquidSample liquid; liquid.bed=water ? water->ground(world,p) : world.elevation(p);
    liquid.level=liquid.bed;
    const auto river=world.modules().enabled(Module::Rivers) ? world.waterNetwork()->sample(p) : RiverSample{};
    if(river.wet && river.level>world.config().seaLevel+.02) {
        liquid.present=true;liquid.river=true;liquid.level=river.level;
        if(world.modules().enabled(Module::WaterMotion)) liquid.current=river.downstream*river.speed;
    } else if(world.modules().enabled(Module::Ocean) && liquid.bed<world.config().seaLevel) {
        liquid.ocean=true; liquid.present=true; liquid.level=world.config().seaLevel;
        if(motion) liquid.current=motion->current(p);
    } else if(water) {
        const auto ground=water->sample(p);
        if(ground.available && ground.depth*ground.weight>0.0005) {
            liquid.present=true; liquid.level=liquid.bed+ground.depth*ground.weight;
            liquid.current=water->flowVector(ground.velocity); liquid.sediment=ground.sedimentKgM3;
        }
    }
    if(liquid.present && motion && !liquid.river) liquid.level+=motion->waves(p,liquid.level-liquid.bed,liquid.ocean).height;
    liquid.depth=std::max(0.0,liquid.level-liquid.bed);
    return liquid;
}
} // namespace planet

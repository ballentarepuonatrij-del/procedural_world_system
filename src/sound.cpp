#include "planet/sound.hpp"
#include <fstream>
#include <stdexcept>

namespace planet {
double speedOfSound(double t) {
    if(!std::isfinite(t) || t<150 || t>370) throw std::invalid_argument("Acoustic temperature must be 150..370 K");
    return std::sqrt(1.4*gasConstantAir*t);
}
double thunderDelay(double distance,double t) {
    if(!std::isfinite(distance) || distance<0) throw std::invalid_argument("Invalid acoustic distance");
    return distance/speedOfSound(t);
}
SoundScene soundScene(const World& world,const Simulation& simulation,const Explorer& explorer,double volume,bool paused,bool globe) {
    SoundScene s;s.seed=world.config().seed;s.listener=explorer.up()*(world.config().radius+explorer.altitude());s.right=explorer.right();
    s.volume=volume;s.underwater=explorer.underwater();s.sheltered=explorer.underground();
    s.audible=world.modules().enabled(Module::Sound) && !paused && !globe &&
        (world.modules().enabled(Module::Atmosphere) || s.underwater);
    if(!s.audible) return s;
    s.thunderEnabled=simulation.global.enabled() && simulation.global.cloudsEnabled();
    const auto air=simulation.weather.sample(explorer.up());const auto climate=world.sample(explorer.up(),simulation.weather.seconds());
    s.temperature=std::isfinite(climate.temperature) ? clamp(climate.temperature,180.0,340.0) : 288.15;
    const double speed=length(air.velocity)*.7;
    s.wind=clamp(speed/45,0.0,1.0);s.windPan=speed>0 ? clamp(-dot(air.velocity,explorer.right())*.7/speed,-1.0,1.0) : 0;
    s.rain=clamp(std::sqrt((air.rainRate*3600+simulation.rainExperiment())/80),0.0,1.0);
    if(world.modules().enabled(Module::SurfaceDetail) && world.modules().enabled(Module::Biomes) && !climate.ocean) {
        const double vegetation=(climate.biome==Biome::Forest || climate.biome==Biome::Rainforest) ? 1 :
            (climate.biome==Biome::Grassland ? .3 : (climate.biome==Biome::Tundra ? .05 : 0));
        s.rustle=vegetation*clamp(speed/14,0.0,1.0);
    }
    const auto liquid=liquidAt(world,&simulation.water,&simulation.motion,explorer.up());
    double proximity=liquid.ocean ? 1 : 0;
    if(world.modules().enabled(Module::Ocean) && !liquid.ocean && !s.sheltered) {
        for(int i=0;i<8;++i) {
            const auto toward=east(explorer.up())*std::cos(tau*i/8)+north(explorer.up())*std::sin(tau*i/8);
            for(double distance:{20.,60.,150.}) if(world.elevation(normalized(explorer.up()+toward*(distance/world.config().radius)))<world.config().seaLevel) {
                const double weight=std::exp(-distance/70);
                if(weight>proximity) {proximity=weight;s.waterPan=dot(toward,explorer.right());}break;
            }
        }
    }
    if(world.modules().enabled(Module::WaterMotion)) s.water=proximity*clamp(simulation.motion.significantHeight()/3,0.0,1.0);
    if(!s.underwater) s.water*=std::exp(-std::max(0.0,explorer.altitude()-world.surface(explorer.up()))/100);
    if(liquid.present && liquid.depth>.01) s.water=clamp(s.water+length(liquid.current)*.35,0.0,1.0);
    if(s.sheltered) {s.rain=0;s.rustle=0;}
    if(!s.underwater && world.modules().enabled(Module::Atmosphere)) {
        for(const auto& strike:simulation.global.lightning()) {
            if(length(strike.direction-explorer.up())*world.config().radius>21000) continue;
            const auto source=strike.direction*(world.config().radius+world.surface(strike.direction));
            const double distance=length(source-s.listener);
            if(distance>20000) continue;
            const auto toward=normalized(source-s.listener);
            s.strikes.push_back({hash64(strike.id^hash64(strike.cycle)^hash64(strike.generation+51)),distance,
                clamp(dot(toward,explorer.right()),-1.0,1.0),strike.strength,simulation.global.effectsTime()-strike.time,source});
        }
    }
    return s;
}
SoundSynth::SoundSynth(std::uint64_t seed) {reset(seed);}
void SoundSynth::reset(std::uint64_t seed) {
    scene_={};scene_.seed=seed;filters_={};voices_={};seen_={};nextSeen_=seenCount_=0;
    rng_={static_cast<std::uint32_t>(hash64(seed+91237))|1U,static_cast<std::uint32_t>(hash64(seed+15279))|1U};
    volume_=wind_=rain_=water_=rustle_=under_=shelter_=gust_=phase_=0;samples_=0;thunderCount_=0;initialized_=false;
}
double SoundSynth::noise(std::uint32_t& state) {
    state^=state<<13;state^=state>>17;state^=state<<5;
    return static_cast<double>(state)/2147483648.0-1;
}
void SoundSynth::setScene(const SoundScene& scene) {
    for(double x:{scene.volume,scene.wind,scene.rain,scene.water,scene.rustle})
        if(!std::isfinite(x) || x<0 || x>1) throw std::invalid_argument("Acoustic levels must be 0..1");
    for(double x:{scene.windPan,scene.waterPan}) if(!std::isfinite(x) || std::abs(x)>1) throw std::invalid_argument("Invalid acoustic pan");
    if(!std::isfinite(length(scene.listener)) || !std::isfinite(length(scene.right)) || std::abs(length(scene.right)-1)>1e-6 || scene.strikes.size()>16)
        throw std::invalid_argument("Invalid acoustic scene");
    (void)speedOfSound(scene.temperature);
    for(const auto& s:scene.strikes) if(!std::isfinite(s.distance) || s.distance<0 || s.distance>20000 || !std::isfinite(s.age) || s.age<0 ||
       !std::isfinite(s.pan) || std::abs(s.pan)>1 || !std::isfinite(s.strength) || s.strength<0 || s.strength>2 || !std::isfinite(length(s.source)))
        throw std::invalid_argument("Invalid thunder event");
    if(scene.seed!=scene_.seed) reset(scene.seed);
    if(!scene.audible || !scene.thunderEnabled || (initialized_ && length(scene.listener-scene_.listener)>1000)) for(auto& voice:voices_) voice.active=false;
    scene_=scene;initialized_=true;
    for(auto& v:voices_) if(v.active && length(v.source)>0)
        v.pan=clamp(dot(normalized(v.source-scene.listener),scene.right),-1.0,1.0);
    for(const auto& strike:scene.strikes) {
        const auto end=seen_.begin()+static_cast<std::ptrdiff_t>(seenCount_);
        if(std::find(seen_.begin(),end,strike.key)!=end) continue;
        seen_[nextSeen_++%seen_.size()]=strike.key;seenCount_=std::min(seen_.size(),seenCount_+1);
        const double delay=thunderDelay(strike.distance,scene.temperature)-strike.age;
        // Ignore sounds whose arrival was skipped by a teleport or a fast clock.
        if(!scene.audible || !scene.thunderEnabled || delay<-.1 || strike.strength==0) continue;
        auto slot=std::find_if(voices_.begin(),voices_.end(),[](const Voice& v){return !v.active;});
        if(slot==voices_.end()) continue;
        *slot={true,static_cast<std::uint64_t>(std::max(0.0,delay)*sampleRate),0,
            static_cast<std::uint64_t>((4+strike.distance/4000)*sampleRate),
            .7*strike.strength*600/(400+strike.distance),strike.pan,
            1-std::exp(-tau*(200+4000*std::exp(-strike.distance/5000))/sampleRate),0,0,
            static_cast<std::uint32_t>(hash64(strike.key+183))|1U,strike.source};
        ++thunderCount_;
    }
}
std::size_t SoundSynth::activeThunder() const {return static_cast<std::size_t>(std::count_if(voices_.begin(),voices_.end(),[](const Voice& v){return v.active;}));}
void SoundSynth::render(std::span<std::int16_t> pcm) {
    if(pcm.size()%channels) throw std::invalid_argument("Audio buffer must contain stereo frames");
    constexpr double smooth=1.0/(sampleRate*.04),slow=1.0/(sampleRate*.12);
    const double target=scene_.audible ? scene_.volume : 0;
    const std::array<double,2> windPan{std::sqrt((1-scene_.windPan*.5)/2),std::sqrt((1+scene_.windPan*.5)/2)};
    const std::array<double,2> waterPan{std::sqrt((1-scene_.waterPan*.6)/2),std::sqrt((1+scene_.waterPan*.6)/2)};
    for(std::size_t frame=0;frame<pcm.size()/channels;++frame) {
        volume_+=(target-volume_)*smooth;if(target==0 && volume_<1e-6) volume_=0;
        wind_+=(scene_.wind-wind_)*slow;rain_+=(scene_.rain-rain_)*slow;water_+=(scene_.water-water_)*slow;rustle_+=(scene_.rustle-rustle_)*slow;
        under_+=(static_cast<double>(scene_.underwater)-under_)*smooth;shelter_+=(static_cast<double>(scene_.sheltered)-shelter_)*smooth;
        std::array<double,2> thunder{};
        for(auto& v:voices_) if(v.active) {
            if(v.delay) {--v.delay;continue;}
            const double t=static_cast<double>(v.age)/sampleRate;
            const double n=noise(v.rng);v.low+=(n-v.low)*v.cutoff;v.rumble+=(n-v.rumble)*.008;
            const double envelope=std::min(1.0,t/.012)*std::exp(-t/(1.0+static_cast<double>(v.duration)/sampleRate*.18));
            const double tail=clamp((static_cast<double>(v.duration-v.age)/sampleRate)/.25,0.0,1.0);
            const double sound=(v.low*std::exp(-t*7)*1.2+v.rumble*7)*envelope*tail*v.amplitude;
            thunder[0]+=sound*std::sqrt((1-v.pan)/2);thunder[1]+=sound*std::sqrt((1+v.pan)/2);
            if(++v.age>=v.duration) v.active=false;
        }
        const double gustNoise=noise(rng_[0]);gust_+=(gustNoise-gust_)*.00007;
        const double gust=clamp(.75+gust_*7,.25,1.3);
        phase_+=tau*(85+wind_*160)/sampleRate;if(phase_>tau) phase_-=tau;
        const double surge=.55+.35*std::sin(tau*static_cast<double>(samples_)/(sampleRate*5.7));
        for(unsigned ch=0;ch<channels;++ch) {
            auto& f=filters_[ch];const double n=noise(rng_[ch]),rainNoise=noise(rng_[ch]),seaNoise=noise(rng_[ch]);
            f.windLow+=(n-f.windLow)*.13;f.windHigh+=(n-f.windHigh)*.006;
            f.rainLow+=(rainNoise-f.rainLow)*.55;f.rainHigh+=(rainNoise-f.rainHigh)*.055;
            f.seaLow+=(seaNoise-f.seaLow)*.08;f.seaHigh+=(seaNoise-f.seaHigh)*.002;
            const double wind=((f.windLow-f.windHigh)*.58+std::sin(phase_)*.022*wind_)*wind_*gust*windPan[ch];
            const double rain=(f.rainLow-f.rainHigh)*.27*rain_;
            const double waves=(f.seaLow-f.seaHigh)*1.2*water_*surge*waterPan[ch];
            const double leaves=(n-f.windLow)*.065*rustle_*gust;
            double sound=wind+rain+waves+leaves+thunder[ch];
            f.under+=(sound-f.under)*.035;f.cave+=(sound-f.cave)*.12;
            sound=mix(sound,f.under*.32,under_);sound=mix(sound,f.cave*.12,shelter_);
            f.dc+=(sound-f.dc)*.0007;sound=(sound-f.dc)*volume_;
            // Continuous soft limiting; PCM always remains strictly inside full scale.
            sound=.9*sound/(1+std::abs(sound));
            pcm[frame*channels+ch]=static_cast<std::int16_t>(std::lround(sound*32767));
        }
        ++samples_;
    }
}
void writeWave(const std::filesystem::path& path,std::span<const std::int16_t> pcm) {
    if(pcm.size()%2 || pcm.size()>SoundSynth::sampleRate*SoundSynth::channels*60)
        throw std::invalid_argument("WAV export requires stereo PCM of at most 60 seconds");
    std::ofstream out(path,std::ios::binary);if(!out) throw std::runtime_error("Cannot open WAV output");
    const auto word=[&](std::uint32_t value,unsigned bytes) {for(unsigned i=0;i<bytes;++i) out.put(static_cast<char>((value>>(8*i))&255U));};
    const auto size=static_cast<std::uint32_t>(pcm.size()*sizeof(std::int16_t));
    out.write("RIFF",4);word(36+size,4);out.write("WAVEfmt ",8);word(16,4);word(1,2);word(2,2);
    word(SoundSynth::sampleRate,4);word(SoundSynth::sampleRate*4,4);word(4,2);word(16,2);out.write("data",4);word(size,4);
    for(const auto value:pcm) word(static_cast<std::uint16_t>(value),2);
    out.flush();
    if(!out) throw std::runtime_error("Cannot write WAV output");
}
} // namespace planet

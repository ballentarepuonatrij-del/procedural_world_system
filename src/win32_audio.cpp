#include "planet/win32_audio.hpp"
#include <mmsystem.h>
#include <chrono>

namespace planet {
WavePlayer::WavePlayer(bool record):record_(record) {
    wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!wake_) {error_=MMSYSERR_NOMEM;return;}
    try {worker_=std::thread([this]{run();});} catch(...) {error_=MMSYSERR_NOMEM;}
}
WavePlayer::~WavePlayer() {stop();if(wake_) CloseHandle(wake_);}
void WavePlayer::stop() {
    stop_=true;if(wake_) SetEvent(wake_);
    if(worker_.joinable()) worker_.join();
}
void WavePlayer::update(const SoundScene& scene) {
    std::lock_guard lock(mutex_);scene_=scene;
}
void WavePlayer::saveCapture(const std::filesystem::path& path) {stop();writeWave(path,recording_);}
void WavePlayer::run() {
    constexpr unsigned blockFrames=1024,blocks=4;
    struct Block {std::array<std::int16_t,blockFrames*2> pcm{};WAVEHDR header{};bool prepared=false,queued=false;};
    std::array<Block,blocks> buffers{};HWAVEOUT device=nullptr;
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=SoundSynth::sampleRate;
    format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
    MMRESULT result=waveOutOpen(&device,WAVE_MAPPER,&format,reinterpret_cast<DWORD_PTR>(wake_),0,CALLBACK_EVENT);
    if(result==MMSYSERR_NOERROR) {
        for(auto& b:buffers) {
            b.header.lpData=reinterpret_cast<LPSTR>(b.pcm.data());b.header.dwBufferLength=static_cast<DWORD>(b.pcm.size()*sizeof(std::int16_t));
            result=waveOutPrepareHeader(device,&b.header,sizeof(WAVEHDR));if(result!=MMSYSERR_NOERROR) break;
            b.prepared=true;
        }
        if(result==MMSYSERR_NOERROR) available_=true;
    }
    if(result!=MMSYSERR_NOERROR) error_=result;
    try {
        SoundSynth synth;
        const auto fill=[&](Block& block) {
            SoundScene latest;{std::lock_guard lock(mutex_);latest=scene_;}
            synth.setScene(latest);synth.render(block.pcm);frames_+=blockFrames;thunderCount_=synth.thunderCount();
            unsigned peak=peak_;
            for(const auto sample:block.pcm) peak=std::max(peak,static_cast<unsigned>(std::abs(static_cast<int>(sample))));
            peak_=peak;
            if(record_ && recording_.size()<SoundSynth::sampleRate*2*60) {
                const auto count=std::min<std::size_t>(block.pcm.size(),SoundSynth::sampleRate*2*60-recording_.size());
                recording_.insert(recording_.end(),block.pcm.begin(),block.pcm.begin()+static_cast<std::ptrdiff_t>(count));
            }
        };
        if(available_) {
            while(!stop_) {
                for(auto& b:buffers) {
                    if(b.queued && !(b.header.dwFlags&WHDR_DONE)) continue;
                    if(b.queued) {++completed_;b.queued=false;}
                    if(stop_) break;
                    fill(b);result=waveOutWrite(device,&b.header,sizeof(WAVEHDR));
                    if(result!=MMSYSERR_NOERROR) {error_=result;available_=false;stop_=true;break;}
                    b.queued=true;++submitted_;
                }
                if(!stop_) WaitForSingleObject(wake_,20);
            }
        } else if(record_) {
            // A missing sound device does not prevent diagnostic synthesis/capture.
            // submitted/completed stay zero: a WAV is not evidence of playback.
            using Clock=std::chrono::steady_clock;auto deadline=Clock::now();
            while(!stop_) {
                if(Clock::now()>=deadline) {
                    fill(buffers[0]);deadline+=std::chrono::microseconds(1000000*blockFrames/SoundSynth::sampleRate);
                    if(Clock::now()-deadline>std::chrono::milliseconds(100)) deadline=Clock::now();
                }
                WaitForSingleObject(wake_,5);
            }
        }
    } catch(...) {error_=MMSYSERR_ERROR;available_=false;}
    if(device) {
        waveOutReset(device);
        for(auto& b:buffers) if(b.prepared) waveOutUnprepareHeader(device,&b.header,sizeof(WAVEHDR));
        waveOutClose(device);
    }
}
} // namespace planet

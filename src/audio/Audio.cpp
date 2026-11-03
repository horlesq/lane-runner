#include "audio/Audio.hpp"
#include <algorithm>
#include <cmath>

namespace {

    constexpr float kPi = 3.14159265f;

    // Small deterministic noise source, so every run sounds the same.
    class Noise {
    public:
        float next() {
            state_ = state_ * 1664525u + 1013904223u;
            return static_cast<float>(state_ >> 8) / 8388608.0f - 1.0f;   // -1..1
        }

    private:
        std::uint32_t state_ = 22222u;
    };

    std::size_t frames(int sampleRate, float seconds) {
        return static_cast<std::size_t>(static_cast<float>(sampleRate) * seconds);
    }

    // Square-wave beep with an exponential fade, appended to clip.
    void appendTone(std::vector<float>& clip, int sampleRate, float freq, float seconds, float volume, float decay) {
        const std::size_t n = frames(sampleRate, seconds);
        for (std::size_t i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            const float phase = std::fmod(freq * t, 1.0f);
            const float square = phase < 0.5f ? 1.0f : -1.0f;
            const float attack = std::min(1.0f, static_cast<float>(i) / 80.0f);   // avoids a click
            clip.push_back(square * volume * attack * std::exp(-decay * t));
        }
    }

    void appendSilence(std::vector<float>& clip, int sampleRate, float seconds) {
        clip.insert(clip.end(), frames(sampleRate, seconds), 0.0f);
    }

} // namespace

Audio::Audio() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        return;
    }
    subsystem_ = true;

    SDL_AudioSpec want{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = &Audio::callback;
    want.userdata = this;

    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);   // SDL converts if the device differs
    if (device_ == 0) {
        return;
    }
    sampleRate_ = have.freq;

    buildClips();
    SDL_PauseAudioDevice(device_, 0);   // start the callback only once the clips exist
}

Audio::~Audio() {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);   // stops the audio thread before the clips are destroyed
    }
    if (subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void Audio::play(Sfx sfx) {
    if (device_ == 0) {
        return;
    }
    const std::vector<float>* clip = &clips_[static_cast<std::size_t>(sfx)];

    // The audio thread reads voices_ concurrently, so changes happen under the device lock.
    SDL_LockAudioDevice(device_);
    Voice* target = &voices_[0];
    for (Voice& v : voices_) {
        if (v.clip == nullptr) {
            target = &v;
            break;
        }
        if (v.pos > target->pos) {
            target = &v;   // all busy: replace the voice that has played longest
        }
    }
    target->clip = clip;
    target->pos = 0;
    SDL_UnlockAudioDevice(device_);
}

void Audio::setEngine(bool on, float rev) {
    engineOn_.store(on);
    engineRev_.store(std::clamp(rev, 0.0f, 1.0f));
}

void Audio::callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<Audio*>(userdata);
    self->mix(reinterpret_cast<float*>(stream), len / static_cast<int>(sizeof(float)));
}

void Audio::mix(float* out, int frameCount) {
    const float sr = static_cast<float>(sampleRate_);
    const bool engineOn = engineOn_.load();
    const float targetFreq = 38.0f + 95.0f * engineRev_.load();
    const float targetVolume = engineOn ? 0.11f : 0.0f;

    for (int i = 0; i < frameCount; ++i) {
        float s = 0.0f;

        for (Voice& v : voices_) {
            if (v.clip == nullptr) {
                continue;
            }
            s += (*v.clip)[v.pos++];
            if (v.pos >= v.clip->size()) {
                v.clip = nullptr;
            }
        }

        // Engine: sawtooth plus a sub-octave square, low-pass filtered into a hum.
        engineFreq_ += (targetFreq - engineFreq_) * 0.0004f;
        engineVolume_ += (targetVolume - engineVolume_) * 0.0008f;
        if (engineVolume_ > 0.0005f) {
            enginePhase_ = std::fmod(enginePhase_ + engineFreq_ / sr, 1.0f);
            engineSubPhase_ = std::fmod(engineSubPhase_ + 0.5f * engineFreq_ / sr, 1.0f);
            const float saw = 2.0f * enginePhase_ - 1.0f;
            const float sub = engineSubPhase_ < 0.5f ? 1.0f : -1.0f;
            engineFiltered_ += (0.6f * saw + 0.4f * sub - engineFiltered_) * 0.06f;
            s += engineFiltered_ * engineVolume_;
        }

        out[i] = std::tanh(s);   // soft limiter: overlapping sounds never clip harshly
    }
}

void Audio::buildClips() {
    const int sr = sampleRate_;
    auto clip = [this](Sfx sfx) -> std::vector<float>& { return clips_[static_cast<std::size_t>(sfx)]; };
    Noise noise;

    appendTone(clip(Sfx::MenuMove), sr, 880.0f, 0.05f, 0.16f, 40.0f);

    appendTone(clip(Sfx::MenuSelect), sr, 660.0f, 0.06f, 0.18f, 10.0f);
    appendTone(clip(Sfx::MenuSelect), sr, 990.0f, 0.14f, 0.18f, 14.0f);

    appendTone(clip(Sfx::Denied), sr, 150.0f, 0.08f, 0.20f, 6.0f);
    appendSilence(clip(Sfx::Denied), sr, 0.03f);
    appendTone(clip(Sfx::Denied), sr, 110.0f, 0.14f, 0.20f, 6.0f);

    for (float freq : {523.0f, 659.0f, 784.0f}) {
        appendTone(clip(Sfx::Start), sr, freq, 0.08f, 0.16f, 8.0f);
    }
    appendTone(clip(Sfx::Start), sr, 1046.0f, 0.25f, 0.16f, 6.0f);

    // Lane change: a short burst of noise whose filter opens and closes ("whoosh").
    {
        std::vector<float>& c = clip(Sfx::LaneChange);
        const std::size_t n = frames(sr, 0.18f);
        float filtered = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            const float x = static_cast<float>(i) / static_cast<float>(n);   // 0..1
            const float shape = std::sin(kPi * x);
            filtered += (noise.next() - filtered) * (0.03f + 0.25f * shape);
            c.push_back(filtered * shape * 0.5f);
        }
    }

    // Collision warning: sharp high double beep, like a car's forward-collision chime.
    appendTone(clip(Sfx::CollisionWarning), sr, 1760.0f, 0.07f, 0.16f, 6.0f);
    appendSilence(clip(Sfx::CollisionWarning), sr, 0.05f);
    appendTone(clip(Sfx::CollisionWarning), sr, 1760.0f, 0.07f, 0.16f, 6.0f);

    // Emergency brake: tyre squeal (a wavering high tone) over hissing filtered noise.
    {
        std::vector<float>& c = clip(Sfx::EmergencyBrake);
        const std::size_t n = frames(sr, 0.7f);
        float filtered = 0.0f;
        float phase = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sr);
            const float x = static_cast<float>(i) / static_cast<float>(n);
            const float envelope = std::min(1.0f, t * 40.0f) * (1.0f - x) * (1.0f - x);
            const float freq = 2100.0f + 180.0f * std::sin(2.0f * kPi * 23.0f * t) - 400.0f * x;
            phase = std::fmod(phase + freq / static_cast<float>(sr), 1.0f);
            filtered += (noise.next() - filtered) * 0.35f;
            const float squeal = std::sin(2.0f * kPi * phase);
            c.push_back(envelope * (0.16f * squeal + 0.22f * filtered));
        }
    }

    // Crash: low thump + filtered noise that darkens as it fades + gritty sample-and-hold crunch.
    {
        std::vector<float>& c = clip(Sfx::Crash);
        const std::size_t n = frames(sr, 0.9f);
        float filtered = 0.0f;
        float held = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sr);
            const float thump = std::sin(2.0f * kPi * (55.0f - 20.0f * t) * t) * std::exp(-7.0f * t);
            filtered += (noise.next() - filtered) * (0.04f + 0.5f * std::exp(-6.0f * t));
            if (i % 9 == 0) {
                held = noise.next();
            }
            const float crunch = held * std::exp(-14.0f * t);
            c.push_back(0.8f * thump + 0.9f * filtered * std::exp(-4.0f * t) + 0.3f * crunch);
        }
    }
}

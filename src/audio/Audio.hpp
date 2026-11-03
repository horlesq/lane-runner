#pragma once
#include <SDL.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

enum class Sfx : std::uint8_t {
    MenuMove, MenuSelect, Denied, Start, LaneChange, Crash, CollisionWarning, EmergencyBrake, Skid, Count
};

// Synthesised sound effects and an engine hum. No audio files: every sound is generated
// once at startup, so nothing is allocated while the game runs.
// If no audio device is available the game simply runs silently.
class Audio {
public:
    Audio();
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool ok() const { return device_ != 0; }

    void play(Sfx sfx);

    // Engine hum: on/off and how hard it revs (0 = idle, 1 = top speed). Safe to call every frame.
    void setEngine(bool on, float rev);

private:
    struct Voice {
        const std::vector<float>* clip = nullptr;   // null = voice is free
        std::size_t pos = 0;
    };

    static void callback(void* userdata, Uint8* stream, int len);
    void mix(float* out, int frames);
    void buildClips();

    bool subsystem_ = false;
    SDL_AudioDeviceID device_ = 0;
    int sampleRate_ = 44100;

    std::array<std::vector<float>, static_cast<std::size_t>(Sfx::Count)> clips_;
    std::array<Voice, 8> voices_{};   // guarded by SDL_LockAudioDevice

    // Written by the game thread, read by the audio thread.
    std::atomic<bool> engineOn_{false};
    std::atomic<float> engineRev_{0.0f};

    // Owned by the audio thread only.
    float enginePhase_ = 0.0f;
    float engineSubPhase_ = 0.0f;
    float engineFreq_ = 40.0f;
    float engineVolume_ = 0.0f;
    float engineFiltered_ = 0.0f;
};

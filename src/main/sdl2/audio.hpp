/*********************************************************************
 * CannonBall‑SE – SDL Audio Module
 *
 * Description:
 *   Updated SDL‑based audio for CannonBall‑SE.
 *   Improves playback quality and reliability, and provides
 *   WAV/MP3 playback with background loading giving near‑instant
 *   start on low‑power SD‑card systems such as the Raspberry Pi 2.
 *
 * Copyright (c) 2025 James Pearce.
 *
 * Derived from:
 *   • SDL audio code Copyright (c) 2012, 2020 Chris White
 *   • PCM/YM chip handling (c) 1998‑2008 Atari800 development team,
 *     licensed under GPL‑2.0‑or‑later (see docs/license_atari800.txt).
 *
 * This file is part of CannonBall‑SE.
 * The full text of the Cannonball licence is in docs/license.txt.
 *********************************************************************/

#pragma once

#include "globals.hpp"
#include "frontend/config.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <array>
#include <string>
#include <SDL.h>
#include <semaphore>

#ifdef COMPILE_SOUND_CODE


// A Q15-style table mapping slider 1–8 to roughly –8 dB to +6 dB.
// This is used to allow the user to control the playback volume of
// custom .wav files using an 8-step scale in the menu.
// wav/mp3 file sample values are then multipled by these constants,
// and then >> 13 (/8192).
// This avoids expensive floating point division or mod() in the sound
// mixing loop.
static constexpr int32_t WAV_VOL_TABLE[9] = {
    0,      // unused index 0
    3261,   // 1 ≈  –8 dB → 0.40x
    4106,   // 2 ≈  –6 dB → 0.50x
    5169,   // 3 ≈  –4 dB → 0.63x
    6507,   // 4 ≈  –2 dB → 0.79x
    8192,   // 5 ≈  –0 dB → 1.00x
    10313,  // 6 ≈  +2 dB → 1.26×
    12983,  // 7 ≈  +4 dB → 1.58×
    16345   // 8 ≈  +6 dB → 2.00×
};

static constexpr int32_t WAV_THRESHOLD_TABLE[9] = {
    0,       // unused index 0
    24000,   // 1 ≈  –8 dB → 0.40x
    16345,   // 2 ≈  –6 dB → 0.50x
    12983,   // 3 ≈  –4 dB → 0.63x
    10313,   // 4 ≈  –2 dB → 0.79x
    8192,    // 5 ≈  –0 dB → 1.00x
    6507,    // 6 ≈  +2 dB → 1.26×
    5169,    // 7 ≈  +4 dB → 1.58×
    4106     // 8 ≈  +6 dB → 2.00×
};


class Audio
{
public:
    // Enable/Disable Sound
    bool sound_enabled;

    // In-class Constructor
    Audio()
      : sound_enabled(false),
        FREQ(0),
        bits_per_sample(BITS),
        mix_buffer_bytes(0),
        audio_paused(1),
        dev(0),
        wavfile{ /*filename*/ "", /*data*/ nullptr, /*total_length*/0, /*loaded_length*/ 0,
                 /*pos*/0, /*streaming*/ false, /*fully_loaded*/false, /*stopping*/false }
    {}

    ~Audio();

    void init();
    void start_audio(bool list_devices_only = false);
    void pause_audio();
    void resume_audio();
    void stop_audio();
    void clear_wav();
    void load_audio(const char* filename);

    // Multiplayer must be able to restart an already loaded MP3/WAV without
    // opening the file again from the critical START1 race-init path. The
    // decoder/mixer already protects wavfile with wav_mutex, so rewinding only
    // the read cursor is thread-safe and does not touch the loader thread.
    bool rewind_audio_file()
    {
        std::lock_guard<std::mutex> lock(wav_mutex);

        if (!wavfile.data || wavfile.loaded_length == 0)
            return false;

        wavfile.pos = 0;
        return true;
    }

    void tick();
    void fill_and_mix(uint8_t *stream, int len);

private:
    // Stereo. Could be changed, requires some recoding.
    static const uint32_t CHANNELS = 2;
    // 16-Bit Audio Output. Could be changed, requires some recoding.
    static const uint32_t BITS = 16;

    // Sample Rate
    uint32_t FREQ;
    int bits_per_sample;
    uint32_t mix_buffer_bytes; // Each frame is 250 samples and 8ms at 31250kHz sample rate.
    int audio_paused;
    SDL_AudioDeviceID dev;

    void clear_buffers();

    // wave file related
    struct wav_t {
        std::string filename = "";
        int16_t*   data                 = nullptr;   // interleaved PCM samples
        uint32_t   total_length         = 0;         // full sample‐count (int16_t) for entire file
        uint32_t   loaded_length        = 0;         // how many samples have actually been read so far
        uint32_t   pos                  = 0;         // read‐cursor for playback
        uint32_t   fade_pos             = 0;         // position where we start to loop
        bool       streaming            = false;     // true once we've buffered the initial threshold
        bool       fully_loaded         = false;     // true after the loader thread finishes
        bool       stopping             = false;
    };

    // threading
    std::thread        wav_loader_thread;
    std::mutex         wav_mutex;
    std::atomic<bool>  wav_job_pending{false};

    // temporary storage for loaded buffer
    int16_t*           new_data    = nullptr;
    size_t             new_length  = 0;
    bool               new_loaded  = false;

    wav_t wavfile;

    void thread_load_wav(std::string filename);
    void load_wav(const char* filename);

    // new semaphore queue based approach
    // Ring buffer for PCM frames (8ms each)
    static constexpr int BUFFER_COUNT = 4;
    std::vector<int16_t> ringBuffer[BUFFER_COUNT];
    std::atomic<int> prodIndex{0}, consIndex{0};
    std::counting_semaphore<BUFFER_COUNT> spaceAvailable{BUFFER_COUNT};
    std::counting_semaphore<BUFFER_COUNT> samplesReady{0};
    std::atomic<bool> running{false};

    std::thread mixThread;
    void mixing_loop();
    static void sdl_callback_trampoline(void* udata, Uint8* stream, int len);
};
#endif

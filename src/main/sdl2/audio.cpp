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

#include <iostream>
#include <SDL.h>
#include <mpg123.h>         // to enable mp3 file music

#include "sdl2/wav123.hpp"  // WAV "mpg123-like" decoder
#include <cctype>
#include <algorithm>        // for std::min & std::clamp
#include <cstring>

#include "sdl2/audio.hpp"

//#include "frontend/config.hpp" // fps
#include "engine/audio/osoundint.hpp"

// MP3 loader references ostats to avoid sample generation on CPU-intensive start line
#include "engine/ostats.hpp"

#ifdef COMPILE_SOUND_CODE


// Helper block for audio file loader selection (mpg123 or wav123)
namespace {
struct MpgLike {
    int  (*init)();
    void (*exit)();
    void* (*newh)(const char*, int*);
    int  (*open)(void*, const char*);
    int  (*format_none)(void*);
    int  (*format)(void*, long, int, int);
    off_t(*length)(void*);
    int  (*read)(void*, unsigned char*, size_t, size_t*);
    int  (*close)(void*);
    void (*del)(void*);
    const char* (*strerror)(void*);
    const char* (*plain_strerror)(int);
    int  enc_signed_16;
    int  ok, err, done;
};

static inline bool has_ext_ci(const std::string& path, const char* ext) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = path.substr(dot + 1);
    for (auto& ch : e)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return e == ext;
}

static const MpgLike kMp3Api = {
    &mpg123_init, &mpg123_exit,
    (void*(*)(const char*,int*))&mpg123_new,
    (int(*)(void*,const char*))&mpg123_open,
    (int(*)(void*))&mpg123_format_none,
    (int(*)(void*,long,int,int))&mpg123_format,
    (off_t(*)(void*))&mpg123_length,
    (int(*)(void*,unsigned char*,size_t,size_t*))&mpg123_read,
    (int(*)(void*))&mpg123_close,
    (void(*)(void*))&mpg123_delete,
    (const char*(*)(void*))&mpg123_strerror,
    &mpg123_plain_strerror,
    MPG123_ENC_SIGNED_16,
    MPG123_OK, MPG123_ERR, MPG123_DONE
};

static const MpgLike kWavApi = {
    &wav123_init, &wav123_exit,
    (void*(*)(const char*,int*))&wav123_new,
    (int(*)(void*,const char*))&wav123_open,
    (int(*)(void*))&wav123_format_none,
    (int(*)(void*,long,int,int))&wav123_format,
    (off_t(*)(void*))&wav123_length,
    (int(*)(void*,unsigned char*,size_t,size_t*))&wav123_read,
    (int(*)(void*))&wav123_close,
    (void(*)(void*))&wav123_delete,
    (const char*(*)(void*))&wav123_strerror,
    &wav123_plain_strerror,
    WAV123_ENC_SIGNED_16,
    WAV123_OK, WAV123_ERR, WAV123_DONE
};
} // namespace


// MP3/Wav length of cross-fade on repeat
// 2^18 samples at 44.1kHz stereo is about 3 seconds
static constexpr uint32_t FADE_BITS    = 18;
static constexpr uint32_t FADE_LEN     = (1u << FADE_BITS);


Audio::~Audio()
{
    stop_audio();
    mpg123_exit();
    wav123_exit();
}

void Audio::init()
{
    if (config.sound.enabled) {
        // Initialize MP3 decoder (once)
        if (mpg123_init() != MPG123_OK) {
            std::cerr << "Failed to init mpg123: " << mpg123_plain_strerror(MPG123_ERR) << "\n";
        }
        if (wav123_init() != 0) {
            std::cerr << "Failed to init wav123\n";
        }
        start_audio();
        bits_per_sample = BITS;
        // std::cout << "Audio::init: Bits per sample: " << bits_per_sample << "\n";
    }
}


void Audio::start_audio(bool list_devices_only)
{
    if (!sound_enabled)
    {
        // Since many GNU/Linux distros are infected with PulseAudio, SDL2 could chose PA as first
        // driver option before ALSA, and PA doesn't obbey our sample number requests, resulting
        // in audio gaps, if we're on a GNU/Linux we force ALSA.
        // Else we accept whatever SDL2 wants to give us or what the user specifies on SDL_AUDIODRIVER
        // enviroment variable.
        std::string platform = SDL_GetPlatform();
        if (platform=="Linux") {

            if (SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) {
                std::cout << "Error initalizing audio subsystem: " << SDL_GetError() << std::endl;
            }

            if (SDL_AudioInit("alsa")!=0) {
//            if (SDL_AudioInit("")!=0) {
                std::cout << "Error initalizing audio using ALSA: " << SDL_GetError() << std::endl;
                return;
            }

        }
        else {
            if(SDL_Init(SDL_INIT_AUDIO) == -1) {
                std::cout << "Error initalizing audio: " << SDL_GetError() << std::endl;
                return;
            }
        }

        // Display available devices, user may wish to use a particular device e.g. external DAC
        printf("Available audio devices:\n");
        int numDevices = SDL_GetNumAudioDevices(0); // 0 requests playback devices
        if (numDevices > 32) {
            // clamp to 32 max, probably way more than any setup will have
            numDevices = 32;
        }
        const char* device_name[32];
        memset(device_name, 0, sizeof(device_name));

        for (int i = 0; i < numDevices; i++) {
            device_name[i] = SDL_GetAudioDeviceName(i, 0);
            printf("   %d: %s\n", i, device_name[i]);
        }

        if (list_devices_only)
            // request was to list the available SDL devices only, this is used in the main program
            // with command-line option -list-sound-devices to help the user chose the sound device
            // during the build process.
            return;

        // SDL Audio Properties
        SDL_AudioSpec desired, obtained;

        desired.freq     = FREQ = config.sound.rate;
        desired.format   = AUDIO_S16SYS;
        desired.channels = CHANNELS;
        // The arcade used a fixed 8ms audio tick rate. This is problematic when running under WSL, which needs a 16ms
        // tick to play smoothly.
        if (config.sound.callback_rate==0) {
            desired.samples = FREQ/125;
        } else {
            desired.samples = 2*FREQ/125;
        }
        desired.callback = Audio::sdl_callback_trampoline;
        desired.userdata = this;

        const char* playback_device = NULL;
        if (config.sound.playback_device != -1 && config.sound.playback_device < numDevices) {
            // User has configured a particular output device; find its name
            playback_device = device_name[config.sound.playback_device];
        }

        // SDL2 block
        dev = SDL_OpenAudioDevice(playback_device, 0, &desired, &obtained, /*SDL_AUDIO_ALLOW_FORMAT_CHANGE*/0);
        if (dev == 0) {
                std::cerr << "Error opening audio device: " << SDL_GetError() << std::endl;
                return;
            }

            // JJP info line
            std::cout << "Requested Sample Rate: " << desired.freq << ", ";
            std::cout << "SDL Returned Configured Sample Rate: " << obtained.freq << std::endl;
            FREQ = obtained.freq;

            if (desired.samples != obtained.samples) {
                std::cerr << "Error initalizing audio: number of samples not supported." << std::endl
                          << "Please compare desired vs obtained. Look at what audio driver SDL2 is using." << std::endl;
            return;
        }

        // new SDL buffering - start paused; will be un-paused in first tick()
        SDL_PauseAudioDevice(dev, 1);

        mix_buffer_bytes = obtained.samples * CHANNELS * (BITS / 8);

        clear_buffers();
        clear_wav();

        // Ready for Generating Audio
        sound_enabled = true;
        audio_paused = 1;
    }
}

void Audio::pause_audio()
{
    if (sound_enabled)
    {
        SDL_PauseAudioDevice(dev,1);
        audio_paused = 2;
    }
}

void Audio::resume_audio()
{
    if (sound_enabled)
    {
        clear_buffers();
        audio_paused = 1; // audio_tick() will commence playback when there's something to play
    }
}

void Audio::stop_audio()
{
    if (!sound_enabled) return;

    SDL_PauseAudioDevice(dev, 1);
    SDL_LockAudioDevice(dev);

//    running.store(false);
//    spaceAvailable.release(BUFFER_COUNT);
    // Fix for Windows to avoid over-release of counting semaphore
    running.store(false, std::memory_order_relaxed);
    if (mixThread.joinable()) mixThread.join();

    audio_paused = 2;
    clear_wav();
    SDL_UnlockAudioDevice(dev);
    SDL_CloseAudioDevice(dev);
    sound_enabled = false;
}

void Audio::clear_buffers()
{
    SDL_LockAudioDevice(dev);
    SDL_ClearQueuedAudio(dev);
    // Allocate (or re-allocated) mix buffers
    for (auto &buf : ringBuffer) buf.resize(mix_buffer_bytes/2);  // div 2 as int16
    SDL_UnlockAudioDevice(dev);
}

// Updated tick() function, should be called every frame
void Audio::tick()
{
    // 1) Return if sound is globally disabled or explicitly paused
    if (!sound_enabled || audio_paused == 2)
        return;

    // 2) Unpause the audio if necessary.
    if (audio_paused) {
        // Drop anything still queued
        clear_buffers();

        running.store(true);
        // launch mixing thread
        mixThread = std::thread(&Audio::mixing_loop, this);

        // and unpause the device
        SDL_PauseAudioDevice(dev, 0);
        audio_paused = 0;
    }
}


/* Linux safe version
void Audio::mixing_loop() {
    while (running.load()) {
        // wait for space
        spaceAvailable.acquire();
        // fill next buffer slot
        auto &dst = ringBuffer[prodIndex.load()];
        fill_and_mix(reinterpret_cast<uint8_t*>(dst.data()), mix_buffer_bytes);
        // signal data ready
        samplesReady.release();
        prodIndex = (prodIndex + 1) % BUFFER_COUNT;
    }
}
*/
// JJP - Cross-platform safe version; avoids over-release of counting semaphore
void Audio::mixing_loop() {
    while (running.load(std::memory_order_relaxed)) {
        // wait for space, but time out so we can re-check 'running'
        if (!spaceAvailable.try_acquire_for(std::chrono::milliseconds(2))) {
            continue; // timed out; loop back and re-check 'running'
        }
        auto &dst = ringBuffer[prodIndex.load(std::memory_order_relaxed)];
        fill_and_mix(reinterpret_cast<uint8_t*>(dst.data()), mix_buffer_bytes);
        samplesReady.release();
        prodIndex = (prodIndex + 1) % BUFFER_COUNT;
    }
}


void Audio::fill_and_mix(uint8_t *stream, int len)
{
    // Call-back routine - provides SDL with 8ms (or 16ms if config.sound.callback_rate != 0) of audio samples

    int16_t *out       = reinterpret_cast<int16_t*>(stream);
    int cycles         = (config.sound.callback_rate == 0 ? 1 : 2);
    uint32_t samples   = static_cast<uint32_t>(len >> cycles);
    static int fadein  = 0;
    //(config.sound.callback_rate == 0 ? len : len >> 1);

    if (audio_paused) return;

    while (cycles--) {
        // 1) drive the sound chips
        osoundint.tick();
        osoundint.pcm->stream_update();
        osoundint.ym ->stream_update();

        auto* pcm_buf = osoundint.pcm->get_buffer();
        auto* ym_buf  = osoundint.ym ->get_buffer();
        if (osoundint.pcm->buffer_size < samples)
            samples = osoundint.pcm->buffer_size;

        // 2) mix +/- optional WAV
        if (wavfile.streaming) {
            std::lock_guard<std::mutex> lock(wav_mutex);
            uint32_t pos       = wavfile.pos;
            uint32_t fade_pos  = wavfile.fade_pos;
            uint32_t avail     = wavfile.loaded_length - pos; // number of samples we could use
            uint32_t total_len = wavfile.total_length;

            uint32_t i = 0;
            // first part - run till lesser or avail and samples
            uint32_t end = std::min(samples, avail);

            if (wavfile.fully_loaded && (pos >= fade_pos) && (fade_pos > 0)) {
                // near the end of the trade, fade out tail and fail-in head)
                for (; i < end; ++i, ++pos, ++fadein) {
                    float m = float(fadein) / FADE_LEN;
                    int32_t tail_samp = (int32_t(wavfile.data[pos]) * WAV_VOL_TABLE[config.sound.wave_volume]) >> 13;
                    int32_t head_samp = (int32_t(wavfile.data[fadein]) * WAV_VOL_TABLE[config.sound.wave_volume]) >> 13;
                    int32_t wf = int32_t(tail_samp*(1.0f - m) + head_samp); // head_samp*m to fade-in repeat track

                    // add generated audio and mixed wav, then clamp to 16-bit
                    int32_t s = pcm_buf[i] + ym_buf[i] + wf;
                    *out++ = static_cast<int16_t>(std::clamp(s, -32768, 32767));
                }
            }

            // wrap around if we're at the end of the file
            if (pos >= total_len) {
                pos     = fadein; // continue after the fadein
                fadein  = 0;
                avail   = wavfile.loaded_length - pos;
                end     = std::min(samples, avail);
            }

            // Process the remainder of this callback from the start of the file
            for (; i < end; ++i) {
                int32_t s = pcm_buf[i] + ym_buf[i];
                // mix in wave/mp3 sample
                s += (int32_t(wavfile.data[pos++]) * WAV_VOL_TABLE[config.sound.wave_volume]) >> 13;
                *out++ = static_cast<int16_t>(std::clamp(s, -32768, 32767));
            }

            // check position again (should never meet this case)
            if (pos >= total_len) {
                pos     = 0;
                fadein  = 0;
            }

            // Finally anything left from just generate audio (wav file might be still loading)
            for (; i < samples; ++i) {
                int32_t s = pcm_buf[i] + ym_buf[i];
                *out++ = static_cast<int16_t>(std::clamp(s, -32768, 32767));
            }
            wavfile.pos = pos;
        } else {
            // no wav/mp3 playing - mix PCM+YM and output
            for (uint32_t i = 0; i < samples; ++i) {
                int32_t s = pcm_buf[i] + ym_buf[i];
                *out++ = static_cast<int16_t>(std::clamp(s, -32768, 32767));
            }
        }
    }
}


void Audio::sdl_callback_trampoline(void* udata, Uint8* stream, int len)
{
    auto* self = static_cast<Audio*>(udata);
    // wait until a buffer is ready
    self->samplesReady.acquire();
    // copy one buffer
    auto &src = self->ringBuffer[self->consIndex.load()];
    memcpy(stream, src.data(), len);
    self->consIndex = (self->consIndex + 1) % BUFFER_COUNT;
    // free space for mixer
    self->spaceAvailable.release();
}


// ***********************************************************************************
//
// Wave/MP3 music related functions follow
//
// ***********************************************************************************


void Audio::load_audio(const char* filename)
{
    std::string fn = filename;
    auto dot = fn.find_last_of('.');
    if (dot == std::string::npos) {
        std::cerr << "Audio::load_audio: no file extension on " << filename << std::endl;
        return;
    }
    std::string ext = fn.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(tolower(c));

    // Check if we already have loaded (or are loading) this file under the lock
    {
        std::lock_guard<std::mutex> lock(wav_mutex);
        if (wavfile.streaming) {
            if (wavfile.filename == filename) {
                // file is loading and probably playing; do nothing
                // note - this test prevents the music reloading between radio screen and game start
                return;
            } else {
                wavfile.stopping = true;
            }
        }
    }

    if (wav_loader_thread.joinable()) {
        // something playing or at least loading, wait for it to finish then clear up
        wav_loader_thread.join();
        clear_wav();
    }

    // now launch a new loader
    if ((ext == "wav") || (ext == "mp3")) {
        load_wav(filename);
    }
    else {
        std::cerr << "Audio::load_audio: unsupported format (not mp3/wav)" << ext << std::endl;
    }
}


// Empty Wav Buffer
static int16_t EMPTY_BUFFER[] = {0, 0, 0, 0};


void Audio::load_wav(const char* filename)
{
    // Launch the thread
    wav_loader_thread = std::thread(&Audio::thread_load_wav, this, std::string(filename));
}


void Audio::thread_load_wav(std::string filename)
{
    // runs as a background task and triggers playback once 2 seconds of data has been processed.
    // Handles .mp3 (via mpg123) and .wav (via wav123)
    const bool is_wav = has_ext_ci(filename, "wav") || has_ext_ci(filename, "wave");
    const MpgLike& api = is_wav ? kWavApi : kMp3Api;

    int err = 0;
    void* h = api.newh(nullptr, &err);
    if (!h) {
        std::cerr << (is_wav ? "wav123_new" : "mpg123_new")
                  << " failed: " << api.plain_strerror(err) << std::endl;
        return;
    }
    if (api.open(h, filename.c_str()) != api.ok) {
        std::cerr << (is_wav ? "wav123_open" : "mpg123_open")
                  << " failed: " << api.strerror(h) << std::endl;
        api.del(h);
        return;
    }

    // Force output format to our mixer settings: signed 16-bit, CHANNELS, FREQ
    if (api.format_none(h) != api.ok ||
        api.format(h, FREQ, CHANNELS, api.enc_signed_16) != api.ok) {
        std::cerr << (is_wav ? "wav123_format" : "mpg123_format")
                  << " failed\n";
        api.close(h); api.del(h);
        return;
    }

    // 1) Determine total sample count (frames × channels) at output rate
    const off_t frames = api.length(h);
    size_t total_samples = frames > 0 ? static_cast<size_t>(frames) * CHANNELS
                                      : (FREQ * 60 * CHANNELS); // fallback 60s if unknown
    const size_t threshold = FREQ * 2 * CHANNELS; // begin playback after ~2s decoded

    // 2) Allocate/initialize shared buffer state
    {
        std::lock_guard<std::mutex> lock(wav_mutex);
        if (wavfile.data && wavfile.data != EMPTY_BUFFER)
            std::free(wavfile.data);
        wavfile.data          = (int16_t*)std::malloc(std::max(threshold, total_samples) * sizeof(int16_t));
        wavfile.filename      = filename;
        wavfile.total_length  = total_samples;
        wavfile.loaded_length = 0;
        wavfile.pos           = 0;
        wavfile.fade_pos      = 0;
        wavfile.streaming     = false;
        wavfile.fully_loaded  = false;
        wavfile.stopping      = false;
    }

    // 3) Stream-decode and append into the shared buffer as data arrives
    std::vector<unsigned char> buf(16384); // bytes - about 90ms assuming 44.1kHz, 16-bit stereo
    size_t i_samples = 0;
    bool stopping = false;

    // wait time in-game is based around decoding one block per frame at 30fps, since the Pi Zero/1 will be CPU bound
    auto wait_time            = std::chrono::duration<double>(1.0 / 30);
    // wait time on music selector is less, we can afford to decode more there
    auto wait_time_init_music = std::chrono::duration<double>(1.0 / 120);
    uint32_t buffer_min = (FREQ * CHANNELS) >> 3; // 125ms worth of data

    while (true) {
        auto nextframe = std::chrono::steady_clock::now() +
                         ((outrun.game_state == GS_INIT_MUSIC) ?
                             wait_time_init_music
                           : wait_time);
        {
            std::lock_guard<std::mutex> lock(wav_mutex);
            stopping = wavfile.stopping;
        }
        if (stopping) break;

        // check to see if we're just moving away from the start line
        if (!(
              ((outrun.game_state == GS_START2) ||
               (outrun.game_state == GS_START3) ||
               (outrun.game_state == GS_INGAME))    &&                  // in game,
              (ostats.cur_stage  == 0)              &&                  // first stage,
              (ostats.stage_times[0][0] == 0)       &&                  // less than 0 minutes and...
              (ostats.stage_times[0][1]  < 2)       &&                  // ...2 seconds past go,
              (wavfile.streaming)                   &&                  // playing an mp3/wav, and
              ((wavfile.loaded_length - wavfile.pos) > buffer_min)))    // we have > 125ms of audio on-hand, then:
        {
            // Generate next sample block
            size_t done = 0;
            const int r = api.read(h, buf.data(), buf.size(), &done);
            if (done > 0) {
                const size_t samples = done / sizeof(int16_t);
                std::lock_guard<std::mutex> lock(wav_mutex);
                if (!stopping) {
                    size_t room = (wavfile.total_length > wavfile.loaded_length)
                                  ? (wavfile.total_length - wavfile.loaded_length)
                                  : 0;
                    size_t to_copy = std::min(samples, room);
                    std::memcpy(wavfile.data + wavfile.loaded_length, buf.data(), to_copy * sizeof(int16_t));
                    wavfile.loaded_length += to_copy;
                    i_samples += to_copy;
                    if (!wavfile.streaming && wavfile.loaded_length >= threshold) {
                        wavfile.streaming = true; // okay to start playback now
                    }
                }
            }

            if (r == api.done) break;
            if (r == api.err) {
                std::cerr << (is_wav ? "wav123_read" : "mpg123_read") << " failed\n";
                break;
            }
        }

        // throttle CPU load of decoding; insert a brief sleep (if we're not trying to
        // if we're not loading the first 2 seconds)
        if (wavfile.streaming) {
            auto this_wait_time = nextframe - std::chrono::steady_clock::now();
            if (this_wait_time < std::chrono::milliseconds(5))
                this_wait_time = std::chrono::milliseconds(5);
            if (this_wait_time > std::chrono::milliseconds(30))
                this_wait_time = std::chrono::milliseconds(30);
            std::this_thread::sleep_for(this_wait_time);
        }
    }

    api.close(h);
    api.del(h);

    if (!stopping) {
        // 4) adjust the length to avoid quiet part of track end (abrupt cutoff will be masked by fade)
        long lowerthreshold = (long(6144) * WAV_THRESHOLD_TABLE[config.sound.wave_volume]) >> 13;
        uint32_t i = i_samples;
        while (i>0) { if (wavfile.data[--i] > lowerthreshold) break; }
        if (i>0) i_samples = i; // chop off any quiet bit at the end
        // 5) Mark load complete; set fade position (for cross-fade on repeat)
        {
            std::lock_guard<std::mutex> lock(wav_mutex);
            wavfile.total_length  = i_samples; // trim to what we actually filled
            wavfile.loaded_length = wavfile.total_length;
            wavfile.fully_loaded  = true;
            wavfile.fade_pos      =   (wavfile.total_length > FADE_LEN)
                                    ? (wavfile.total_length - FADE_LEN) : 0;
        }
    }
}


void Audio::clear_wav()
{
    // If a previous wave load job is still running, wait for it to finish
    {
        std::lock_guard<std::mutex> lock(wav_mutex);
        wavfile.stopping = true;
    }
    if (wav_loader_thread.joinable())
        wav_loader_thread.join();

    SDL_LockAudioDevice(dev);
    {
        std::lock_guard<std::mutex> lock(wav_mutex);

        if (wavfile.fully_loaded)
            std::free(wavfile.data);

        wavfile.data           = EMPTY_BUFFER;
        wavfile.filename       = "";
        wavfile.total_length   = 1;
        wavfile.loaded_length  = 1;
        wavfile.pos            = 0;
        wavfile.fade_pos       = 0;
        wavfile.streaming      = false;
        wavfile.fully_loaded   = false;
        wavfile.stopping       = false;
    }
    SDL_UnlockAudioDevice(dev);
}

#endif

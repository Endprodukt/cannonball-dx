/***************************************************************************
    XML Configuration File Handling.

    Load Settings.
    Load & Save Hi-Scores.

    Copyright Chris White.
    See license.txt for more details.

    This version for CannonBall-SE incorporates revisions that are
    Copyright (c) 2025 James Pearce:
    - Refactored to remove Boost dependencies using xml_loader.h shim and
      C++20 function equivalents
    - Automatic custom music loader
    - CRT effect handling settings
    - Play stats load/save
***************************************************************************/

#include <iostream>
#include <fstream>   // for std::ifstream / std::ofstream
#include <iterator>  // for std::istreambuf_iterator
#include <filesystem>
#include <regex>
#include <map>
#include <algorithm>
#include <cctype>
#include <string>
#include <cstdio>   // remove()
#include <sstream>

#include "main.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "../utils.hpp"

#include "engine/ohiscore.hpp"
#include "engine/outils.hpp"
#include "engine/audio/osoundint.hpp"
#include "sdl2/pixel_scaler_state.hpp"
#include "sdl2/gamepad_rumble_state.hpp"
#include "engine/car_palette_state.hpp"


Config config;

Config::Config(void)
{
    data.cfg_file = "config.xml";

    // Setup default sounds
    music_t magical, breeze, splash;
    magical.title = "MAGICAL SOUND SHOWER";
    breeze.title  = "PASSING BREEZE";
    splash.title  = "SPLASH WAVE";
    magical.type  = music_t::IS_YM_INT;
    breeze.type   = music_t::IS_YM_INT;
    splash.type   = music_t::IS_YM_INT;
    magical.cmd   = sound::MUSIC_MAGICAL;
    breeze.cmd    = sound::MUSIC_BREEZE;
    splash.cmd    = sound::MUSIC_SPLASH;
    sound.music.push_back(magical); // 1st slot
    sound.music.push_back(breeze);  // 2nd slot
    sound.music.push_back(splash);  // 3rd slot
    // Users can replace these with custom music via .wav, .mp3, or .ym files in the res/ folder,
    // and/or add additional tracks.
    sound.custom_tracks_loaded = 0;
}

Config::~Config(void)
{
}

void Config::get_custom_music(const std::string& respath)
{
    namespace fs = std::filesystem;
#ifdef WITH_MP3
    static const std::map<std::string,int> ext_priority = {
        { "WAV", 0 },
        { "MP3", 1 },
        { "YM", 2 }
    };
#else
    static const std::map<std::string,int> ext_priority = {
        { "WAV", 0 },
        { "YM",  1 }
    };
#endif

    std::map<int, std::pair<std::string,fs::path>> chosen;
    std::regex pattern(R"((\d{2})[-_](.+))");

    for (auto& entry : fs::directory_iterator(respath)) {
        if (!entry.is_regular_file()) continue;

        auto path = entry.path();
        auto ext = path.extension().string();
        if (ext.size()<2) continue;
        ext = ext.substr(1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
        auto prio_it = ext_priority.find(ext);
        if (prio_it == ext_priority.end()) continue;

        auto stem = path.stem().string();
        std::smatch m;
        if (!std::regex_match(stem, m, pattern)) continue;

        int idx = std::stoi(m[1]);      // track index 01–99
        int prio = prio_it->second;
        auto it = chosen.find(idx);
        if (it==chosen.end() || prio < ext_priority.at(it->second.first)) {
            chosen[idx] = { ext, path };
        }
    }

    // apply replacements/additions
    for (auto& [idx, ext_path] : chosen) {
        auto& [ext, filepath] = ext_path;

        // build display name in uppercase
        std::string raw = filepath.stem().string().substr(3); // drop "NN_"
        std::replace(raw.begin(), raw.end(), '_', ' ');
        std::replace(raw.begin(), raw.end(), '-', ' ');
        std::transform(raw.begin(), raw.end(), raw.begin(), ::toupper);

        // pick type based on extension
        int track_type = (ext == "YM")
            ? music_t::IS_YM_EXT
            : music_t::IS_WAV;   // WAV & MP3 both map to IS_WAV

        int cmd = sound::MUSIC_CUSTOM;

        std::cout << "Found music file " << raw;
        ++sound.custom_tracks_loaded;
        music_t entry {
            track_type,
            cmd,
            raw,                  // TITLE (upper‑case)
            filepath.filename().string()
        };

        if (idx >= 1 && idx <= 3) {
            std::cout << " (replacing built-in track " << idx << ")" << std::endl;
            // replace built‑in slot 0–2
            sound.music[idx-1] = entry;
        } else {
            std::cout << " (added as available track)" << std::endl;
            // append new track
            sound.music.push_back(entry);
        }
    }
}

// Set Path to load and save config to
void Config::set_config_file(const std::string& file)
{
    data.cfg_file = file;
}

void Config::load_core()
{
    cfg.clear();
    bool file_found = true;

    // Try current directory
    if (!xml_parser::read_xml(data.cfg_file, cfg)) {
        // settings not found. Flag that we need to create the file.
        file_found = false;
        // std::cerr << "Warning: " << data.cfg_file << " could not be loaded.\n";
        // Try res directory (which should contain a default configuration)
        std::string default_cfg_path = "res/" + data.cfg_file;
        if (!xml_parser::read_xml(default_cfg_path, cfg)) {
            std::cout << "Unable to load config.xml. Using defaults.";
            cfg.clear(); // reset the cfg ptree
        } else {
            std::cout << "Loaded settings from " << default_cfg_path << ".";
        }
        std::cout << " config.xml will be created in current directory.\n";
    }

    // ------------------------------------------------------------------------
    // Master Settings
    // ------------------------------------------------------------------------
    int F10Escape    = cfg.get_int("F10Escape", 0); // default is to use ESCAPE
    master_break_key = F10Escape ? SDLK_F10 : SDLK_ESCAPE;

    // ------------------------------------------------------------------------
    // Data Settings
    // ------------------------------------------------------------------------
    data.rom_path         = cfg.get_string("data.rompath", "roms/");  // Path to ROMs
    data.res_path         = cfg.get_string("data.respath", "res/");   // Path to resources
    data.save_path        = cfg.get_string("data.savepath", "./");    // Path to Save Data
    data.crc32            = cfg.get_int   ("data.crc32", 1);

    data.file_scores      = data.save_path + "hiscores.xml";
    data.file_scores_jap  = data.save_path + "hiscores_jap.xml";
    data.file_ttrial      = data.save_path + "hiscores_timetrial.xml";
    data.file_ttrial_jap  = data.save_path + "hiscores_timetrial_jap.xml";
    data.file_cont        = data.save_path + "hiscores_continuous.xml";
    data.file_cont_jap    = data.save_path + "hiscores_continuous_jap.xml";
    data.file_stats       = data.save_path + "play_stats.xml";

    // ------------------------------------------------------------------------
    // Menu Settings
    // ------------------------------------------------------------------------

    menu.enabled           = cfg.get_int("menu.enabled",   0);
    menu.road_scroll_speed = cfg.get_int("menu.roadspeed", 50);

    // ------------------------------------------------------------------------
    // Video Settings
    // ------------------------------------------------------------------------

    video.mode          = cfg.get_int("video.mode",            1); // Video Mode: Default is Full Screen
    video.scale         = cfg.get_int("video.window.scale",    1); // Video Scale: Default is 1x
    video.fps           = cfg.get_int("video.fps",             2); // 0=30 FPS, 2=60 FPS, 3=120 FPS; default 60
    if (video.fps != 0 && video.fps != 3)
        video.fps = 2;
    video.fps_count     = cfg.get_int("video.fps_counter",     0); // FPS Counter
    video.widescreen    = cfg.get_int("video.widescreen",      0); // Enable Widescreen Mode
    video.hires_next    =
    video.hires         = cfg.get_int("video.hires",           1); // Hi-Resolution Mode
    video.hiresprites   = cfg.get_int("video.hiresprites",     1); // default ON with the default 2X engine resolution
    video.vsync         = cfg.get_int("video.vsync",           0); // Default OFF; user setting still takes precedence
    video.x_offset      = cfg.get_int("video.x_offset",        0); // Offset from calculated image X position
    video.y_offset      = cfg.get_int("video.y_offset",        0); // Offset from calculated image Y position
    // JJP Additional configuration for CRT emulation
    video.shader_mode   = cfg.get_int("video.shader_mode",     2); // shader type: 0 = off (actually pass-through), 1 = fast, 2 = full
    video.shadow_mask   = cfg.get_int("video.shadow_mask",     2); // shadow mask type: 0 = off, 1 = overlay based (fast), 2 = shader based (looks better)
    video.mask_size     = cfg.get_int("video.mask_size",       3); // shadow mask size (3=normal, 4/5/6 for high DPI displays)
    video.maskDim       = cfg.get_int("video.maskDim",        75); // shadow mask type dim multiplier (75=75%)
    video.maskBoost     = cfg.get_int("video.maskBoost",     135); // shadow mask type boost multiplier (135=135%)
    video.scanlines     = cfg.get_int("video.scanlines",       0); // scanlines (0=off, 3=max)
    video.crt_shape     = cfg.get_int("video.crt_shape",       1); // CRT shape overlay on or off
    video.vignette      = cfg.get_int("video.vignette",       30); // amount to dim edges (1=1%)
    video.noise         = cfg.get_int("video.noise",           5); // amount of random noise to add (1=1%)
    video.warpX         = cfg.get_int("video.warpX",           3); // amount of warp to add along X axis (1=1%)
    video.warpY         = cfg.get_int("video.warpY",           4); // amount of warp to add along Y axis (1=1%)
    video.desaturate    = cfg.get_int("video.desaturate",      5); // amount to desaturate the entire image (raises black level) (1=1%)
    video.desaturate_edges = cfg.get_int("video.desaturate_edges", 4); // amount further desaturate towards edges (1=1%)
    video.brightboost   = cfg.get_int("video.brightboost",     0); // relative output brightness (1=1%)
    video.blargg        = cfg.get_int("video.blargg",          1); // Blargg filtering mode, 0=off
    video.saturation    = cfg.get_int("video.saturation",     30); // Blargg filter saturation, -1 to +1
    video.contrast      = cfg.get_int("video.contrast",        0); // Blargg filter contrast, -1 to +1
    video.brightness    = cfg.get_int("video.brightness",      0); // Blargg filter brightness, -1 to +1
    video.sharpness     = cfg.get_int("video.sharpness",       0); // Blargg edge bluring
    video.resolution    = cfg.get_int("video.resolution",      0); // Blargg resolution, -2 to 0
    video.gamma         = cfg.get_int("video.gamma",           0); // Blargg gamma, -3 to +3
    video.hue           = cfg.get_int("video.hue",            -2); // Blargg hue, -10 to +10 => -0.1 to +0.1

    // ------------------------------------------------------------------------
    // Sound Settings
    // ------------------------------------------------------------------------
    sound.enabled     = cfg.get_int("sound.enable",      1);
    sound.rate        = cfg.get_int("sound.rate",        44100);
    sound.advertise   = cfg.get_int("sound.advertise",   1);
    sound.preview     = cfg.get_int("sound.preview",     1);
    sound.fix_samples = cfg.get_int("sound.fix_samples", 1);
    sound.music_timer = cfg.get_int("sound.music_timer", 0);
    // JJP - allow either standard 8ms audio callbacks, or a slower 16ms rate (required with WSL2)
    sound.callback_rate   = cfg.get_int("sound.callback_rate",0);
    // Index of SDL playback device to request, -1 for default
    sound.playback_device = cfg.get_int("sound.playback_device", -1);

    // Custom Music. Search for enabled custom tracks
    get_custom_music(data.res_path);

    if (!sound.music_timer)
        sound.music_timer = MUSIC_TIMER;
    else
    {
        if (sound.music_timer > 99)
            sound.music_timer = 99;
        sound.music_timer = outils::DEC_TO_HEX[sound.music_timer]; // convert to hexadecimal
    }

    // JJP - Wave file playback volume, 1-8 where 4 = no adjustment
    sound.wave_volume = cfg.get_int("sound.wave_volume",4);

    // ------------------------------------------------------------------------
    // SMARTYPI Settings
    // ------------------------------------------------------------------------
    smartypi.enabled = cfg.get_int("smartypi.<xmlattr>.enabled",        0);
    smartypi.ouputs  = cfg.get_int("smartypi.outputs",                  1);
    smartypi.cabinet = cfg.get_int("smartypi.cabinet",                  1);

    // ------------------------------------------------------------------------
    // Controls
    // ------------------------------------------------------------------------
    controls.gear          = cfg.get_int("controls.gear",               2);
    controls.steer_speed   = cfg.get_int("controls.steerspeed",         3);
    controls.pedal_speed   = cfg.get_int("controls.pedalspeed",         4);
    controls.rumble        = cfg.get_float("controls.rumble",           1.25f);
    controls.keyconfig[0]  = cfg.get_int("controls.keyconfig.up",       1073741906);
    controls.keyconfig[1]  = cfg.get_int("controls.keyconfig.down",     1073741905);
    controls.keyconfig[2]  = cfg.get_int("controls.keyconfig.left",     1073741904);
    controls.keyconfig[3]  = cfg.get_int("controls.keyconfig.right",    1073741903);
    controls.keyconfig[4]  = cfg.get_int("controls.keyconfig.acc",      97);
    controls.keyconfig[5]  = cfg.get_int("controls.keyconfig.brake",    122);
    controls.keyconfig[6]  = cfg.get_int("controls.keyconfig.gear1",    103);
    controls.keyconfig[7]  = cfg.get_int("controls.keyconfig.gear2",    104);
    controls.keyconfig[8]  = cfg.get_int("controls.keyconfig.start",    115);
    controls.keyconfig[9]  = cfg.get_int("controls.keyconfig.coin",     99);
    controls.keyconfig[10] = cfg.get_int("controls.keyconfig.menu",     109);
    controls.keyconfig[11] = cfg.get_int("controls.keyconfig.view",     118);
    controls.padconfig[0]  = cfg.get_int("controls.padconfig.acc",      -1);
    controls.padconfig[1]  = cfg.get_int("controls.padconfig.brake",    -1);
    controls.padconfig[2]  = cfg.get_int("controls.padconfig.gear1",    -1);
    controls.padconfig[3]  = cfg.get_int("controls.padconfig.gear2",    -1);
    controls.padconfig[4]  = cfg.get_int("controls.padconfig.start",    -1);
    controls.padconfig[5]  = cfg.get_int("controls.padconfig.coin",     -1);
    controls.padconfig[6]  = cfg.get_int("controls.padconfig.menu",     -1);
    controls.padconfig[7]  = cfg.get_int("controls.padconfig.view",     -1);
    controls.padconfig[8]  = cfg.get_int("controls.padconfig.up",       -1);
    controls.padconfig[9]  = cfg.get_int("controls.padconfig.down",     -1);
    controls.padconfig[10] = cfg.get_int("controls.padconfig.left",     -1);
    controls.padconfig[11] = cfg.get_int("controls.padconfig.right",    -1);
    controls.padconfig[12] = cfg.get_int("controls.padconfig.limit_l",  -1);
    controls.padconfig[13] = cfg.get_int("controls.padconfig.limit_c",  -1);
    controls.padconfig[14] = cfg.get_int("controls.padconfig.limit_r",  -1);
    controls.analog        = cfg.get_int("controls.analog.<xmlattr>.enabled", 1);
    controls.pad_id        = cfg.get_int("controls.pad_id",             0);
    controls.axis[0]       = cfg.get_int("controls.analog.axis.wheel",  -1);
    controls.axis[1]       = cfg.get_int("controls.analog.axis.accel",  -1);
    controls.axis[2]       = cfg.get_int("controls.analog.axis.brake",  -1);
    controls.axis[3]       = cfg.get_int("controls.analog.axis.motor",  -1);
    controls.axis_device[0] = cfg.get_string("controls.analog.axis_device.wheel", "");
    controls.axis_device[1] = cfg.get_string("controls.analog.axis_device.accel", "");
    controls.axis_device[2] = cfg.get_string("controls.analog.axis_device.brake", "");
    controls.axis_device[3] = cfg.get_string("controls.analog.axis_device.motor", "");
    controls.invert[1]     = cfg.get_int("controls.analog.axis.accel.<xmlattr>.invert", 0);
    controls.invert[2]     = cfg.get_int("controls.analog.axis.brake.<xmlattr>.invert", 0);
    controls.asettings[0]  = cfg.get_int("controls.analog.wheel.zone",  75);
    controls.asettings[1]  = cfg.get_int("controls.analog.wheel.dead",  0);

    controls.hat[0] = cfg.get_int("controls.hat.up.index", -1);
    controls.hat[1] = cfg.get_int("controls.hat.down.index", -1);
    controls.hat[2] = cfg.get_int("controls.hat.left.index", -1);
    controls.hat[3] = cfg.get_int("controls.hat.right.index", -1);

    controls.hat_value[0] = cfg.get_int("controls.hat.up.value", 0);
    controls.hat_value[1] = cfg.get_int("controls.hat.down.value", 0);
    controls.hat_value[2] = cfg.get_int("controls.hat.left.value", 0);
    controls.hat_value[3] = cfg.get_int("controls.hat.right.value", 0);

    controls.hat_device[0] = cfg.get_string("controls.hat.up.device", "");
    controls.hat_device[1] = cfg.get_string("controls.hat.down.device", "");
    controls.hat_device[2] = cfg.get_string("controls.hat.left.device", "");
    controls.hat_device[3] = cfg.get_string("controls.hat.right.device", "");

    controls.direction_custom[0] = cfg.get_int("controls.direction_custom.up", 0);
    controls.direction_custom[1] = cfg.get_int("controls.direction_custom.down", 0);
    controls.direction_custom[2] = cfg.get_int("controls.direction_custom.left", 0);
    controls.direction_custom[3] = cfg.get_int("controls.direction_custom.right", 0);

    controls.haptic        = cfg.get_int("controls.analog.haptic.<xmlattr>.enabled",    1);
    controls.ffb_strength  = cfg.get_int("controls.analog.haptic.strength", 50);
    if (controls.ffb_strength < 0)
        controls.ffb_strength = 0;
    else if (controls.ffb_strength > 100)
        controls.ffb_strength = 100;
    controls.centering_strength = cfg.get_int("controls.analog.haptic.centering_strength", 30);
    if (controls.centering_strength < 0)
        controls.centering_strength = 0;
    else if (controls.centering_strength > 100)
        controls.centering_strength = 100;

    // Internal FFB tuning values. These are not user-facing configuration options.
    controls.max_force      = 10000;
    controls.min_force      = 8500;
    controls.force_duration = 1;

    // Remove legacy FFB debug options from existing config files.
    cfg.erase("controls.analog.haptic.max_force");
    cfg.erase("controls.analog.haptic.min_force");
    cfg.erase("controls.analog.haptic.force_duration");

    // ------------------------------------------------------------------------
    // Engine Settings
    // ------------------------------------------------------------------------

    engine.dip_time      = cfg.get_int("engine.time",    0);
    engine.dip_traffic   = cfg.get_int("engine.traffic", 1);

    engine.freeze_timer    = engine.dip_time == 4;
    engine.disable_traffic = engine.dip_traffic == 4;
    engine.dip_time    &= 3;
    engine.dip_traffic &= 3;

    engine.freeplay      = cfg.get_int("engine.freeplay",        1) != 0;
    engine.jap           = cfg.get_int("engine.japanese_tracks", 0);
    engine.prototype     = cfg.get_int("engine.prototype",       0);

    // Additional Level Objects
    engine.level_objects   = cfg.get_int("engine.levelobjects",  1);
    engine.randomgen       = cfg.get_int("engine.randomgen",     1);
    engine.fix_bugs_backup =
    engine.fix_bugs        = cfg.get_int("engine.fix_bugs",      1) != 0;
    engine.fix_timer       = cfg.get_int("engine.fix_timer",     1) != 0;
    engine.layout_debug    = cfg.get_int("engine.layout_debug",   0) != 0;
    engine.hiscore_delete  = cfg.get_int("scores.delete_last_entry", 1);
    engine.hiscore_timer   = cfg.get_int("scores.hiscore_timer", 0);
    engine.new_attract     = cfg.get_int("engine.new_attract",   1) != 0;
    engine.offroad         = cfg.get_int("engine.offroad",       0);
    engine.grippy_tyres    = cfg.get_int("engine.grippy_tyres",  0);
    engine.bumper          = cfg.get_int("engine.bumper",         0);
    engine.turbo           = cfg.get_int("engine.turbo",          0);
    engine.car_pal         = cfg.get_int("engine.car_color",      0);

    if (!engine.hiscore_timer)
        engine.hiscore_timer = HIGHSCORE_TIMER;
    else
    {
        if (engine.hiscore_timer > 99)
            engine.hiscore_timer = 99;
        engine.hiscore_timer = outils::DEC_TO_HEX[engine.hiscore_timer]; // convert to hexadecimal
    }

    // ------------------------------------------------------------------------
    // Time Trial Mode
    // ------------------------------------------------------------------------

    ttrial.laps    = cfg.get_int("time_trial.laps",    3);
    ttrial.traffic = cfg.get_int("time_trial.traffic", 3);
    cont_traffic   = cfg.get_int("continuous.traffic", 3);

    if (!file_found) {
        // create a config file with the defaults
        save_core();
    }
}

bool Config::save_core()
{
    // Update all the settings in the tree

    // Master Settings
    int F10Escape = (master_break_key == SDLK_F10) ? 1 : 0;
    cfg.put_int("F10Escape", F10Escape); // default is to use ESCAPE

    // JJP - CRT emulation settings
    cfg.put_int("video.mode",               video.mode);          // Video Mode: Full Screen (2)
    cfg.put_int("video.window.scale",       video.scale);         // Video Scale: 1x (1)
    cfg.put_int("video.fps",                video.fps);           // Frame Rate: 0=30 FPS, 2=60 FPS, 3=120 FPS
    cfg.put_int("video.fps_counter",        video.fps_count);     // FPS Counter (0)
    cfg.put_int("video.widescreen",         video.widescreen);    // Widescreen Mode (1)
    cfg.put_int("video.vsync",              video.vsync);         // V-Sync (1)
    cfg.put_int("video.hires",              video.hires);         // Game engine hires mode (1=enabled)
    cfg.put_int("video.hiresprites",        video.hiresprites);   // hi-res sprites (1=enabled)
    cfg.put_int("video.x_offset",           video.x_offset);      // X offset
    cfg.put_int("video.y_offset",           video.y_offset);      // Y offset
    // JJP Additional configuration for CRT emulation
    cfg.put_int("video.shader_mode",        video.shader_mode);   // Shader type (Off/Fast/Full) (2)
    cfg.put_int("video.shadow_mask",        video.shadow_mask);   // Shadow mask type (Off/Overlay/Shader) (2)
    cfg.put_int("video.mask_size",          video.mask_size);     // Shadow mask size (3)
    cfg.put_int("video.maskDim",            video.maskDim);       // Shadow mask Dim value (0)
    cfg.put_int("video.maskBoost",          video.maskBoost);     // Shadow mask Boost value (0)
    cfg.put_int("video.scanlines",          video.scanlines);     // Scanlines (0)
    cfg.put_int("video.crt_shape",          video.crt_shape);     // CRT shape overlay (0)
    cfg.put_int("video.vignette",           video.vignette);      // Vignette amount (0)
    cfg.put_int("video.noise",              video.noise);         // Noise amount (0)
    cfg.put_int("video.warpX",              video.warpX);         // Warp on X axis (0)
    cfg.put_int("video.warpY",              video.warpY);         // Warp on Y axis (0)
    cfg.put_int("video.desaturate",         video.desaturate);    // Desaturation level (0)
    cfg.put_int("video.desaturate_edges",   video.desaturate_edges); // Edge desaturation (0)
    cfg.put_int("video.brightboost",        video.brightboost);   // Bright boost element 1 (0)
    cfg.put_int("video.blargg",             video.blargg);        // Blargg filtering mode (0=off)
    cfg.put_int("video.saturation",         video.saturation);    // Filter saturation (-1 to +1)
    cfg.put_int("video.contrast",           video.contrast);      // Filter contrast (-1 to +1)
    cfg.put_int("video.brightness",         video.brightness);    // Filter brightness (-1 to +1)
    cfg.put_int("video.sharpness",          video.sharpness);     // Edge blurring
    cfg.put_int("video.resolution",         video.resolution);    // Resolution (-2 to 0)
    cfg.put_int("video.gamma",              video.gamma);         // Gamma (-3 to +3)
    cfg.put_int("video.hue",                video.hue);           // Hue (-10 to +10)

    cfg.put_int("sound.enable",             sound.enabled);
    cfg.put_int("sound.advertise",          sound.advertise);
    cfg.put_int("sound.preview",            sound.preview);
    cfg.put_int("sound.fix_samples",        sound.fix_samples);
    cfg.put_int("sound.rate",               sound.rate);             // audio sampling rate e.g. 44100 (Hz)
    cfg.put_int("sound.callback_rate",      sound.callback_rate);    // JJP - 0=8ms callbacks, 1=16ms
    cfg.put_int("sound.playback_device",    sound.playback_device);  // JJP - Index of SDL playback device to request, -1 for default
    cfg.put_int("sound.wave_volume",        sound.wave_volume);      // JJP - volume adjustment to .wav files

    if (config.smartypi.enabled)
        cfg.put_int("smartypi.cabinet",     config.smartypi.cabinet);

    cfg.put_int("controls.gear",            controls.gear);
    cfg.put_float("controls.rumble",        controls.rumble);
    cfg.put_int("controls.steerspeed",      controls.steer_speed);
    cfg.put_int("controls.pedalspeed",      controls.pedal_speed);
    cfg.put_int("controls.keyconfig.up",    controls.keyconfig[0]);
    cfg.put_int("controls.keyconfig.down",  controls.keyconfig[1]);
    cfg.put_int("controls.keyconfig.left",  controls.keyconfig[2]);
    cfg.put_int("controls.keyconfig.right", controls.keyconfig[3]);
    cfg.put_int("controls.keyconfig.acc",   controls.keyconfig[4]);
    cfg.put_int("controls.keyconfig.brake", controls.keyconfig[5]);
    cfg.put_int("controls.keyconfig.gear1", controls.keyconfig[6]);
    cfg.put_int("controls.keyconfig.gear2", controls.keyconfig[7]);
    cfg.put_int("controls.keyconfig.start", controls.keyconfig[8]);
    cfg.put_int("controls.keyconfig.coin",  controls.keyconfig[9]);
    cfg.put_int("controls.keyconfig.menu",  controls.keyconfig[10]);
    cfg.put_int("controls.keyconfig.view",  controls.keyconfig[11]);
    cfg.put_int("controls.padconfig.acc",   controls.padconfig[0]);
    cfg.put_int("controls.padconfig.brake", controls.padconfig[1]);
    cfg.put_int("controls.padconfig.gear1", controls.padconfig[2]);
    cfg.put_int("controls.padconfig.gear2", controls.padconfig[3]);
    cfg.put_int("controls.padconfig.start", controls.padconfig[4]);
    cfg.put_int("controls.padconfig.coin",  controls.padconfig[5]);
    cfg.put_int("controls.padconfig.menu",  controls.padconfig[6]);
    cfg.put_int("controls.padconfig.view",  controls.padconfig[7]);
    cfg.put_int("controls.padconfig.up",    controls.padconfig[8]);
    cfg.put_int("controls.padconfig.down",  controls.padconfig[9]);
    cfg.put_int("controls.padconfig.left",  controls.padconfig[10]);
    cfg.put_int("controls.padconfig.right", controls.padconfig[11]);
    cfg.put_int("controls.analog.<xmlattr>.enabled", controls.analog);
    cfg.put_int("controls.analog.axis.wheel", controls.axis[0]);
    cfg.put_int("controls.analog.axis.accel", controls.axis[1]);
    cfg.put_int("controls.analog.axis.brake", controls.axis[2]);
    cfg.put_int("controls.analog.axis.motor", controls.axis[3]);

    cfg.put_string("controls.analog.axis_device.wheel", controls.axis_device[0]);
    cfg.put_string("controls.analog.axis_device.accel", controls.axis_device[1]);
    cfg.put_string("controls.analog.axis_device.brake", controls.axis_device[2]);
    cfg.put_string("controls.analog.axis_device.motor", controls.axis_device[3]);
    cfg.put_int("controls.analog.axis.accel.<xmlattr>.invert", controls.invert[1]);
    cfg.put_int("controls.analog.axis.brake.<xmlattr>.invert", controls.invert[2]);
    cfg.put_int("controls.analog.wheel.zone", controls.asettings[0]);
    cfg.put_int("controls.analog.wheel.dead", controls.asettings[1]);
    cfg.put_int("controls.analog.haptic.<xmlattr>.enabled", controls.haptic);
    cfg.put_int("controls.analog.haptic.strength", controls.ffb_strength);
    cfg.put_int("controls.analog.haptic.centering_strength", controls.centering_strength);


    cfg.put_int("controls.hat.up.index", controls.hat[0]);
    cfg.put_int("controls.hat.down.index", controls.hat[1]);
    cfg.put_int("controls.hat.left.index", controls.hat[2]);
    cfg.put_int("controls.hat.right.index", controls.hat[3]);

    cfg.put_int("controls.hat.up.value", controls.hat_value[0]);
    cfg.put_int("controls.hat.down.value", controls.hat_value[1]);
    cfg.put_int("controls.hat.left.value", controls.hat_value[2]);
    cfg.put_int("controls.hat.right.value", controls.hat_value[3]);

    cfg.put_string("controls.hat.up.device", controls.hat_device[0]);
    cfg.put_string("controls.hat.down.device", controls.hat_device[1]);
    cfg.put_string("controls.hat.left.device", controls.hat_device[2]);
    cfg.put_string("controls.hat.right.device", controls.hat_device[3]);

    cfg.put_int("controls.direction_custom.up", controls.direction_custom[0]);
    cfg.put_int("controls.direction_custom.down", controls.direction_custom[1]);
    cfg.put_int("controls.direction_custom.left", controls.direction_custom[2]);
    cfg.put_int("controls.direction_custom.right", controls.direction_custom[3]);

    cfg.put_int("engine.freeplay",        (int) engine.freeplay);
    cfg.put_int("engine.time",            engine.freeze_timer ? 4 : engine.dip_time);
    cfg.put_int("engine.traffic",         engine.disable_traffic ? 4 : engine.dip_traffic);
    cfg.put_int("engine.japanese_tracks", engine.jap);
    cfg.put_int("engine.prototype",       engine.prototype);
    cfg.put_int("engine.levelobjects",    engine.level_objects);
    cfg.put_int("engine.fix_bugs",        (int) engine.fix_bugs);
    cfg.put_int("engine.fix_timer",       (int) engine.fix_timer);
    cfg.put_int("engine.new_attract",     engine.new_attract);
    cfg.put_int("engine.offroad",         (int) engine.offroad);
    cfg.put_int("engine.grippy_tyres",    (int) engine.grippy_tyres);
    cfg.put_int("engine.bumper",          (int) engine.bumper);
    cfg.put_int("engine.turbo",           (int) engine.turbo);
    cfg.put_int("engine.car_color",       engine.car_pal);

    cfg.put_int("time_trial.laps",    ttrial.laps);
    cfg.put_int("time_trial.traffic", ttrial.traffic);
    cfg.put_int("continuous.traffic", cont_traffic);

    // Sync back from doc (mirrors original behavior)
    ttrial.laps    = cfg.get_int("time_trial.laps",    3);
    ttrial.traffic = cfg.get_int("time_trial.traffic", 3);
    cont_traffic   = cfg.get_int("continuous.traffic", 3);

    // Write out to the current directory (even if we loaded from res/)
    if (!xml_parser::write_xml(data.cfg_file, cfg)) {
        std::cerr << "Could not save settings to " << data.cfg_file << std::endl;
        return false;
    }
    return true;
}

void Config::load_scores(bool original_mode)
{
    std::string scores_file;

    if (original_mode)
        scores_file = engine.jap ? data.file_scores_jap : data.file_scores;
    else
        scores_file = engine.jap ? data.file_cont_jap : data.file_cont;

    xml_parser::ptree scores("scores");
    // A missing logical score section is normal on first run. Keep the
    // built-in defaults silently; DX stores all live tables in highscores.xml.
    if (!xml_parser::read_xml(scores_file, scores))
        return;

    // Game Scores
    for (int i = 0; i < ohiscore.NO_SCORES; i++)
    {
        score_entry* e = &ohiscore.scores[i];

        std::string xmltag = "score";
        xmltag += Utils::to_string(i);

        e->score    = Utils::from_hex_string(
                        scores.get_string(xmltag + ".score",    "0"));
        e->initial1 =   scores.get_string(xmltag + ".initial1", ".")[0];
        e->initial2 =   scores.get_string(xmltag + ".initial2", ".")[0];
        e->initial3 =   scores.get_string(xmltag + ".initial3", ".")[0];
        e->maptiles = Utils::from_hex_string(
                        scores.get_string(xmltag + ".maptiles", "20202020"));
        e->time     = Utils::from_hex_string(
                        scores.get_string(xmltag + ".time"    , "0"));

        if (e->initial1 == '.') e->initial1 = 0x20;
        if (e->initial2 == '.') e->initial2 = 0x20;
        if (e->initial3 == '.') e->initial3 = 0x20;
    }
}

void Config::save_scores(bool original_mode)
{
    std::string scores_file;

    if (original_mode)
        scores_file = engine.jap ? data.file_scores_jap : data.file_scores;
    else
        scores_file = engine.jap ? data.file_cont_jap : data.file_cont;

    xml_parser::ptree scores("scores");

    for (int i = 0; i < ohiscore.NO_SCORES; i++)
    {
        score_entry* e = &ohiscore.scores[i];

        std::string xmltag = "score";
        xmltag += Utils::to_string(i);

        scores.put_string(xmltag + ".score",    Utils::to_hex_string(e->score));
        // '.' is used to represent space
        scores.put_string(xmltag + ".initial1", e->initial1 == 0x20 ? "." : Utils::to_string((char) e->initial1));
        scores.put_string(xmltag + ".initial2", e->initial2 == 0x20 ? "." : Utils::to_string((char) e->initial2));
        scores.put_string(xmltag + ".initial3", e->initial3 == 0x20 ? "." : Utils::to_string((char) e->initial3));
        scores.put_string(xmltag + ".maptiles", Utils::to_hex_string(e->maptiles));
        scores.put_string(xmltag + ".time",     Utils::to_hex_string(e->time));
    }

    if (!xml_parser::write_xml(scores_file, scores)) {
        std::cerr << "Could not save hiscores to: " << scores_file << std::endl;
    }
}

void Config::load_stats()
{
    std::string stats_file = data.file_stats;

    xml_parser::ptree stats_data("playstats");
    if (!xml_parser::read_xml(stats_file, stats_data)) {
        std::cerr << "Warning: " << stats_file << " could not be loaded." << std::endl;
        stats.playcount = 0;
        stats.runtime   = 0;
        return;
    }

    // Load machine stats from file
    stats.playcount = stats_data.get_int("playcount", 0);
    stats.runtime   = stats_data.get_int("runtime",   0);
}

void Config::save_stats()
{
    std::string stats_file = data.file_stats;

    xml_parser::ptree stats_data("playstats");

    stats_data.put_int("playcount", stats.playcount);
    stats_data.put_int("runtime",   stats.runtime);

    if (!xml_parser::write_xml(stats_file, stats_data)) {
        std::cerr << "Could not save machine stats to: " << stats_file << std::endl;
    }
}

void Config::load_timetrial_scores()
{
    // Counter value that represents 1m 15s 0ms
    static const uint16_t COUNTER_1M_15 = 0x11D0;

    std::string timetrial_file = engine.jap ? config.data.file_ttrial_jap : config.data.file_ttrial;
    xml_parser::ptree timetrial_scores("timetrial_scores");

    if (!xml_parser::read_xml(timetrial_file, timetrial_scores)) {
        std::cerr << "Warning: Could not load time-trial scores from: " << timetrial_file << std::endl;
        for (int i = 0; i < 15; i++)
            ttrial.best_times[i] = COUNTER_1M_15;
        return;
    }

    // Time Trial Scores
    for (int i = 0; i < 15; i++)
    {
        ttrial.best_times[i] = static_cast<uint16_t>(
                                    timetrial_scores.get_int("time_trial.score" + Utils::to_string(i), COUNTER_1M_15)
                               );
    }
}

void Config::save_timetrial_scores()
{
    std::string timetrial_file = engine.jap ? config.data.file_ttrial_jap : config.data.file_ttrial;
    xml_parser::ptree timetrial_scores("timetrial_scores");


    // Time Trial Scores
    for (int i = 0; i < 15; i++) {
        timetrial_scores.put_int("time_trial.score" + Utils::to_string(i), ttrial.best_times[i]);
    }


    if (!xml_parser::write_xml(timetrial_file, timetrial_scores)) {
        std::cerr << "Could not save time trial scores to: " << timetrial_file << std::endl;
    }
}

bool Config::clear_scores()
{
    // Init Default Hiscores
    ohiscore.init_def_scores();

    int deleted = 0;          // number of successful deletions

    auto try_remove = [&](const std::string& path) {
        if (std::remove(path.c_str()) == 0) ++deleted;
    };

    try_remove(data.file_scores);
    try_remove(data.file_scores_jap);
    try_remove(data.file_ttrial);
    try_remove(data.file_ttrial_jap);
    try_remove(data.file_cont);
    try_remove(data.file_cont_jap);

    // DX Endless owns dedicated score files rather than using the
    // Continuous table. Clear both course-set variants as well.
    try_remove(data.save_path + "hiscores_endless.xml");
    try_remove(data.save_path + "hiscores_endless_jap.xml");

    // returns true if at least one file was deleted
    return (deleted > 0);
}

void Config::set_fps(int fps)
{
    video.fps = fps;
    this->fps = video.fps == 0 ? 30 : (video.fps == 3 ? 120 : 60);
    tick_fps = this->fps;
    cannonball::frame_ms = 1000.0 / this->fps;

    /* JJP - Sound initialised in seperate thread so not required here */
}

// Inc time setting from menu
void Config::inc_time()
{
    if (engine.dip_time == 3)
    {
        if (!engine.freeze_timer)
            engine.freeze_timer = 1;
        else
        {
            engine.dip_time = 0;
            engine.freeze_timer = 0;
        }
    }
    else
        engine.dip_time++;
}

// Inc traffic setting from menu
void Config::inc_traffic()
{
    if (engine.dip_traffic == 3)
    {
        if (!engine.disable_traffic)
            engine.disable_traffic = 1;
        else
        {
            engine.dip_traffic = 0;
            engine.disable_traffic = 0;
        }
    }
    else
        engine.dip_traffic++;
}

namespace
{
    void add_legacy_binding(
        controls_settings_t& controls,
        int target,
        int type,
        int index,
        int value,
        const std::string& device)
    {
        if (index < 0)
            return;

        device_binding_t binding;
        binding.target = target;
        binding.type = type;
        binding.index = index;
        binding.value = value;
        binding.device = device.empty() ? "*" : device;
        controls.device_bindings.push_back(binding);
    }

    void migrate_legacy_device_bindings(controls_settings_t& controls)
    {
        // Analog controls already persisted a device signature, so those can
        // be migrated exactly. Old button bindings had no persisted device;
        // "*" deliberately preserves their previous any-device behaviour.
        add_legacy_binding(
            controls,
            device_binding_t::TARGET_STEER,
            device_binding_t::TYPE_AXIS,
            controls.axis[0],
            0,
            controls.axis_device[0]);

        add_legacy_binding(
            controls,
            device_binding_t::TARGET_ACCEL,
            device_binding_t::TYPE_AXIS,
            controls.axis[1],
            0,
            controls.axis_device[1]);

        add_legacy_binding(
            controls,
            device_binding_t::TARGET_BRAKE,
            device_binding_t::TYPE_AXIS,
            controls.axis[2],
            0,
            controls.axis_device[2]);

        static const int PAD_SLOT[] =
        {
            0,  // accelerate
            1,  // brake
            2,  // gear low / toggle
            3,  // gear high
            4,  // start
            5,  // coin
            6,  // menu
            7,  // view change
            15, // direct view 1
            16, // direct view 2
            17, // direct view 3
        };

        static const int TARGET[] =
        {
            device_binding_t::TARGET_ACCEL,
            device_binding_t::TARGET_BRAKE,
            device_binding_t::TARGET_GEAR1,
            device_binding_t::TARGET_GEAR2,
            device_binding_t::TARGET_START,
            device_binding_t::TARGET_COIN,
            device_binding_t::TARGET_MENU,
            device_binding_t::TARGET_VIEW,
            device_binding_t::TARGET_VIEW1,
            device_binding_t::TARGET_VIEW2,
            device_binding_t::TARGET_VIEW3,
        };

        for (int i = 0; i < 11; i++)
        {
            add_legacy_binding(
                controls,
                TARGET[i],
                device_binding_t::TYPE_BUTTON,
                controls.padconfig[PAD_SLOT[i]],
                0,
                "*");
        }
    }

    void disable_migrated_legacy_bindings(controls_settings_t& controls)
    {
        // Directional D-pad/HAT bindings (8-11) and cabinet motor limits
        // (12-14) stay in the original system for now. Everything represented
        // by the matrix is handled by device_bindings instead.
        controls.axis[0] = -1;
        controls.axis[1] = -1;
        controls.axis[2] = -1;
        controls.axis_device[0].clear();
        controls.axis_device[1].clear();
        controls.axis_device[2].clear();

        static const int PAD_SLOT[] =
        {
            0, 1, 2, 3, 4, 5, 6, 7, 15, 16, 17
        };

        for (int slot : PAD_SLOT)
            controls.padconfig[slot] = -1;
    }

    void apply_default_gamepad_legacy_bindings(controls_settings_t& controls)
    {
        // First-run controller profile. These are SDL GameController values,
        // so an Xbox 360/XInput-style pad works immediately without setup.
        controls.gear = controls_settings_t::GEAR_SEPARATE;
        controls.analog = 1;

        controls.axis[0] = SDL_CONTROLLER_AXIS_LEFTX;
        controls.axis[1] = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        controls.axis[2] = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        controls.axis_device[0].clear();
        controls.axis_device[1].clear();
        controls.axis_device[2].clear();
        controls.invert[1] = false;
        controls.invert[2] = false;
        controls.asettings[0] = 0;
        controls.asettings[1] = 0;

        // Triggers are the primary analog pedals. Shoulder buttons remain as
        // convenient digital fallbacks for controllers without usable axes.
        controls.padconfig[0] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        controls.padconfig[1] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        controls.padconfig[2] = SDL_CONTROLLER_BUTTON_A;      // Low / downshift
        controls.padconfig[3] = SDL_CONTROLLER_BUTTON_X;      // High / upshift
        controls.padconfig[4] = SDL_CONTROLLER_BUTTON_START;
        controls.padconfig[5] = SDL_CONTROLLER_BUTTON_BACK;   // Coin / Select
        controls.padconfig[6] = SDL_CONTROLLER_BUTTON_GUIDE;  // Menu Access / Home / PS
        controls.padconfig[7] = SDL_CONTROLLER_BUTTON_Y;      // View

        // D-pad is also handled permanently by Input, but keep the legacy
        // values valid so the generated config is self-explanatory.
        controls.padconfig[8]  = SDL_CONTROLLER_BUTTON_DPAD_UP;
        controls.padconfig[9]  = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        controls.padconfig[10] = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        controls.padconfig[11] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    }

    bool binding_is_gamepad(const device_binding_t& binding)
    {
        return binding.device.size() >= 2 &&
            binding.device.compare(0, 2, "G:") == 0;
    }

    bool gamepad_target_is_bound(
        const controls_settings_t& controls,
        int target)
    {
        for (const auto& binding : controls.device_bindings)
        {
            if (binding.target == target && binding_is_gamepad(binding))
                return true;
        }

        return false;
    }

    void add_default_gamepad_binding(
        controls_settings_t& controls,
        int target,
        int type,
        int index,
        const std::string& device)
    {
        if (gamepad_target_is_bound(controls, target))
            return;

        device_binding_t binding;
        binding.target = target;
        binding.type = type;
        binding.index = index;
        binding.value = 0;
        binding.device = device;
        controls.device_bindings.push_back(binding);
    }

    std::string first_gamecontroller_signature()
    {
        if ((SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK) == 0)
            return std::string();

        const int count = SDL_NumJoysticks();

        for (int i = 0; i < count; i++)
        {
            if (!SDL_IsGameController(i))
                continue;

            SDL_Joystick* joystick = SDL_JoystickOpen(i);
            if (!joystick)
                continue;

            SDL_JoystickGUID guid = SDL_JoystickGetGUID(joystick);
            char guid_string[33] = {};
            SDL_JoystickGetGUIDString(guid, guid_string, sizeof(guid_string));

            const std::string signature =
                std::string(guid_string) +
                "|A" + std::to_string(SDL_JoystickNumAxes(joystick)) +
                "|B" + std::to_string(SDL_JoystickNumButtons(joystick)) +
                "|H" + std::to_string(SDL_JoystickNumHats(joystick));

            SDL_JoystickClose(joystick);
            return signature;
        }

        return std::string();
    }

    bool materialize_default_gamepad_bindings(controls_settings_t& controls)
    {
        const std::string signature = first_gamecontroller_signature();
        if (signature.empty())
            return false;

        const std::string device = "G:" + signature;

        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_STEER,
            device_binding_t::TYPE_AXIS,
            SDL_CONTROLLER_AXIS_LEFTX,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_ACCEL,
            device_binding_t::TYPE_AXIS,
            SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_BRAKE,
            device_binding_t::TYPE_AXIS,
            SDL_CONTROLLER_AXIS_TRIGGERLEFT,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_GEAR1,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_A,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_GEAR2,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_X,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_START,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_START,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_COIN,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_BACK,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_MENU,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_GUIDE,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_VIEW,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_Y,
            device);

        return true;
    }

    bool parse_device_bindings(
        const std::string& encoded,
        std::vector<device_binding_t>& bindings)
    {
        bindings.clear();

        if (encoded.empty())
            return false;

        std::stringstream entries(encoded);
        std::string entry;

        while (std::getline(entries, entry, ';'))
        {
            if (entry.empty())
                continue;

            std::stringstream fields(entry);
            std::string target;
            std::string type;
            std::string index;
            std::string value;
            std::string device;

            if (!std::getline(fields, target, ',') ||
                !std::getline(fields, type, ',') ||
                !std::getline(fields, index, ',') ||
                !std::getline(fields, value, ',') ||
                !std::getline(fields, device))
            {
                continue;
            }

            try
            {
                device_binding_t binding;
                binding.target = std::stoi(target);
                binding.type = std::stoi(type);
                binding.index = std::stoi(index);
                binding.value = std::stoi(value);
                binding.device = device;

                if (binding.target < device_binding_t::TARGET_STEER ||
                    binding.target > device_binding_t::TARGET_VIEW3 ||
                    binding.type < device_binding_t::TYPE_BUTTON ||
                    binding.type > device_binding_t::TYPE_HAT ||
                    binding.index < 0 ||
                    binding.device.empty())
                {
                    continue;
                }

                bindings.push_back(binding);
            }
            catch (...)
            {
                // Ignore malformed entries and keep loading the rest.
            }
        }

        return true;
    }

    std::string encode_device_bindings(
        const std::vector<device_binding_t>& bindings)
    {
        std::ostringstream encoded;
        bool first = true;

        for (const auto& binding : bindings)
        {
            if (binding.index < 0 || binding.device.empty())
                continue;

            if (!first)
                encoded << ';';

            first = false;
            encoded
                << binding.target << ','
                << binding.type << ','
                << binding.index << ','
                << binding.value << ','
                << binding.device;
        }

        return encoded.str();
    }

    bool parent_has_comment(
        tinyxml2::XMLNode* parent,
        const std::string& text)
    {
        if (!parent)
            return false;

        for (tinyxml2::XMLNode* node = parent->FirstChild();
             node;
             node = node->NextSibling())
        {
            if (const tinyxml2::XMLComment* comment = node->ToComment())
            {
                const char* value = comment->Value();
                if (value && text == value)
                    return true;
            }
        }

        return false;
    }

    void add_config_comment_before(
        xml_parser::ptree& cfg,
        const char* path,
        const char* text)
    {
        tinyxml2::XMLElement* element = cfg.find_node(path);
        if (!element || !element->Parent() || !text || !*text)
            return;

        tinyxml2::XMLNode* parent = element->Parent();
        if (parent_has_comment(parent, text))
            return;

        tinyxml2::XMLNode* comment = cfg.doc.NewComment(text);
        if (tinyxml2::XMLNode* previous = element->PreviousSibling())
            parent->InsertAfterChild(previous, comment);
        else
            parent->InsertFirstChild(comment);
    }

    void add_ffb_config_comments(xml_parser::ptree& cfg)
    {
        struct ConfigComment
        {
            const char* path;
            const char* text;
        };

        static const ConfigComment COMMENTS[] =
        {
            { "controls.analog.haptic.effects.sand",
              "FFB effect strengths use 0-100. 0 disables the individual effect; 100 is maximum configured strength." },
            { "controls.analog.haptic.effects.sand",
              "Sand / rough-surface grit taps." },
            { "controls.analog.haptic.effects.tyre_slip",
              "Tyre-slip vibration while sliding on the road." },
            { "controls.analog.haptic.effects.offroad_rumble_one_wheel",
              "Off-road vibration when one side of the car leaves the road." },
            { "controls.analog.haptic.effects.offroad_rumble_full",
              "Off-road vibration when the whole car is off the road." },
            { "controls.analog.haptic.effects.offroad_pull_one_wheel",
              "Outward steering pull when one side of the car is off-road." },
            { "controls.analog.haptic.effects.offroad_pull_full",
              "Outward steering pull when the whole car is off-road." },
            { "controls.analog.haptic.effects.gear_shift",
              "Gear-change kick and rebound." },
            { "controls.analog.haptic.effects.music_selector",
              "Short Music Select step impulse between songs." },
            { "controls.analog.haptic.effects.traffic_skid",
              "Steering yank after a traffic collision / skid." },
            { "controls.analog.haptic.effects.crash_bump",
              "Low-speed scenery impact." },
            { "controls.analog.haptic.effects.crash_spin_impact",
              "Initial impact that starts a scenery spin." },
            { "controls.analog.haptic.effects.crash_spin",
              "Repeated side loads while the car spins." },
            { "controls.analog.haptic.effects.crash_flip_impact",
              "Initial impact that starts a high-speed flip." },
            { "controls.analog.haptic.effects.crash_flip",
              "Repeated / sustained side loads during a flip." },
            { "controls.analog.haptic.effects.crash_flip_landing",
              "Landing impact after a flip." },
            { "controls.analog.haptic.effects.start_steering",
              "Automatic steering load while the Ferrari drives onto the start line." },
            { "controls.analog.haptic.effects.start_rev_shake",
              "Throttle-dependent engine / rev shake before the start." },

            { "controls.analog.haptic.spring.low_speed",
              "Spring strength percentages use 0-100 and are relative to the Spring value selected in the in-game Controls menu." },
            { "controls.analog.haptic.spring.low_speed",
              "Low-speed spring percentage used in menus, Attract Mode, stationary driving and low speed." },
            { "controls.analog.haptic.spring.high_speed",
              "High-speed spring percentage reached at speed_full." },
            { "controls.analog.haptic.spring.sliding",
              "Percentage of the currently active spring retained during on-road tyre slip." },
            { "controls.analog.haptic.spring.speed_start",
              "Vehicle-speed threshold, NOT a percentage. Valid range 0-294; spring starts increasing here." },
            { "controls.analog.haptic.spring.speed_full",
              "Vehicle-speed threshold, NOT a percentage. Valid range 0-294; spring reaches high_speed here and stays there up to the normal maximum speed of 294." },
            { "controls.analog.haptic.spring.traffic_skid",
              "Spring percentage during a traffic-collision skid." },
            { "controls.analog.haptic.spring.crash_bump",
              "Spring percentage during a low-speed scenery bump." },
            { "controls.analog.haptic.spring.crash_spin",
              "Spring percentage during the active scenery spin." },
            { "controls.analog.haptic.spring.crash_recovery",
              "Spring percentage during spin recovery." },
            { "controls.analog.haptic.spring.crash_flip_start",
              "Spring percentage at the start of a flip." },
            { "controls.analog.haptic.spring.crash_flip_airborne",
              "Spring percentage while the car is airborne." },
            { "controls.analog.haptic.spring.crash_flip_transition",
              "Spring percentage through the flip transition." },
            { "controls.analog.haptic.spring.crash_flip_landing",
              "Spring percentage at landing." },
            { "controls.analog.haptic.spring.crash_flip_recovery",
              "Spring percentage during post-flip recovery." },
        };

        for (const ConfigComment& entry : COMMENTS)
            add_config_comment_before(cfg, entry.path, entry.text);
    }
}

void Config::load()
{
    const bool first_run = !std::filesystem::exists(data.cfg_file);

    load_core();

    // Fresh CannonBall DX installs use a MAME-compatible keyboard layout.
    // Existing stored mappings always win; only genuinely absent settings are
    // filled so upgrading never rewrites a user's controls.
    const int KEY_SETTING_MISSING = -0x3fffffff;
    auto apply_keyboard_default = [&](const char* path, int slot, SDL_Keycode key)
    {
        if (first_run || cfg.get_int(path, KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
            controls.keyconfig[slot] = key;
    };

    apply_keyboard_default("controls.keyconfig.up",    0, SDLK_UP);
    apply_keyboard_default("controls.keyconfig.down",  1, SDLK_DOWN);
    apply_keyboard_default("controls.keyconfig.left",  2, SDLK_LEFT);
    apply_keyboard_default("controls.keyconfig.right", 3, SDLK_RIGHT);
    apply_keyboard_default("controls.keyconfig.acc",   4, SDLK_LCTRL);  // MAME Button 1
    apply_keyboard_default("controls.keyconfig.brake", 5, SDLK_LALT);   // MAME Button 2
    apply_keyboard_default("controls.keyconfig.gear1", 6, SDLK_SPACE);  // MAME Button 3
    apply_keyboard_default("controls.keyconfig.gear2", 7, SDLK_LSHIFT); // MAME Button 4
    apply_keyboard_default("controls.keyconfig.start", 8, SDLK_1);      // P1 Start
    apply_keyboard_default("controls.keyconfig.coin",  9, SDLK_5);      // Coin 1
    apply_keyboard_default("controls.keyconfig.menu", 10, SDLK_TAB);    // MAME UI menu
    apply_keyboard_default("controls.keyconfig.view", 11, SDLK_z);      // MAME Button 5

    if (first_run ||
        cfg.get_int("controls.radio.keyboard", KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
    {
        set_radio_key(SDLK_x); // MAME Button 6
    }

    if (first_run ||
        cfg.get_int("controls.system.pause.keyboard", KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
    {
        set_system_action_key(SYSTEM_ACTION_PAUSE, SDLK_p); // MAME Pause
    }
    if (first_run ||
        cfg.get_int("controls.system.accept.keyboard", KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
    {
        set_system_action_key(SYSTEM_ACTION_ACCEPT, SDLK_RETURN);
    }
    if (first_run ||
        cfg.get_int("controls.system.back.keyboard", KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
    {
        set_system_action_key(SYSTEM_ACTION_BACK, SDLK_ESCAPE);
    }

    // CannonBall DX stores every score table in one physical file. Keep the
    // old mode-specific paths as logical selectors, but make Original World
    // point at the real file so the legacy Clear Scores action removes it too.
    data.file_scores = data.save_path + "highscores.xml";
    xml_parser::migrate_highscores(data.save_path);

    // Do not inherit historical CannonBall-SE CRT/filter defaults on first run.
    // This deliberately overrides both the core loader fallbacks and
    // any outdated resource config that may still be present in a build folder.
    if (first_run)
    {
        video.widescreen = 0;
        video.fps = 2;
        video.shader_mode = 0;
        video.shadow_mask = 0;
        video.maskDim = 100;
        video.maskBoost = 100;
        video.scanlines = 0;
        video.crt_shape = 0;
        video.vignette = 0;
        video.noise = 0;
        video.warpX = 0;
        video.warpY = 0;
        video.desaturate = 0;
        video.desaturate_edges = 0;
        video.brightboost = 0;
        video.blargg = 0;
        video.saturation = 0;
        video.contrast = 0;
        video.brightness = 0;
        video.sharpness = 0;
        video.resolution = 0;
        video.gamma = 0;
        video.hue = 0;

        sound.playback_device = -1;

        // Canonical CannonBall DX wheel defaults. These match the tested
        // headroom preset materialized below by seed_ffb_tuning_defaults().
        controls.haptic = 1;
        controls.ffb_strength = 50;
        controls.centering_strength = 60;

        // Ensure an old resource config cannot leak previous FFB values into
        // a freshly generated config.xml.
        cfg.erase("controls.analog.haptic.effects");
        cfg.erase("controls.analog.haptic.spring");
        cfg.put_int("controls.analog.haptic.<xmlattr>.enabled", 1);
        cfg.put_int("controls.analog.haptic.strength", 50);
        cfg.put_int("controls.analog.haptic.centering_strength", 60);

        // Mark the standard Xbox/SDL profile even if an old resource config
        // was loaded. It will be materialized to the physical pad after SDL
        // has initialized and the config is next saved.
        cfg.put_int("controls.default_gamepad", 1);
        apply_default_gamepad_legacy_bindings(controls);
    }
    else if (cfg.get_int("controls.analog.haptic.centering_strength", -1) == -1)
    {
        // Existing configs that never had the basic Spring value use the new
        // CannonBall DX default without changing any explicitly stored value.
        controls.centering_strength = 60;
    }

    // engine.car_pal remains the live/runtime Ferrari colour. Keep a separate
    // persistent default so Music Select can change the race colour without
    // ever overwriting the user's attract/default colour.
    car_palette_state::initialize(engine.car_pal);
    engine.car_pal = car_palette_state::get_default(engine.car_pal);

    int scaler_mode = cfg.get_int("video.pixel_scaler", pixel_scaler::OFF);
    int scaler_last = cfg.get_int("video.pixel_scaler_last", pixel_scaler::XBRZ_4X);

    if (first_run)
        scaler_mode = pixel_scaler::OFF;

    if (!pixel_scaler::valid(scaler_mode))
        scaler_mode = pixel_scaler::OFF;
    if (!pixel_scaler::active(scaler_last))
        scaler_last = pixel_scaler::XBRZ_4X;

    pixel_scaler::last_mode.store(scaler_last, std::memory_order_relaxed);
    pixel_scaler::set(scaler_mode);

    // Rumble enable is deliberately independent from rumble strength. Legacy
    // configs used strength 0 as OFF, so preserve that intent on first load.
    const bool legacy_rumble_enabled = controls.rumble > 0.0f;
    gamepad_rumble::enabled =
        cfg.get_int(
            "controls.rumble_enabled",
            legacy_rumble_enabled ? 1 : 0) != 0;

    // The old default could exceed the menu's 0..1 range. Keep a valid stored
    // strength even while rumble is disabled; the separate enable flag decides
    // whether the motors actually run.
    if (controls.rumble <= 0.0f)
        controls.rumble = 0.5f;
    else if (controls.rumble > 1.0f)
        controls.rumble = 1.0f;

    // Optional direct camera selection bindings. -1 means unassigned.
    controls.keyconfig[12] = cfg.get_int("controls.keyconfig.view1", -1);
    controls.keyconfig[13] = cfg.get_int("controls.keyconfig.view2", -1);
    controls.keyconfig[14] = cfg.get_int("controls.keyconfig.view3", -1);

    // Slots 12-14 remain the original cabinet motor-limit inputs.
    // The three new view buttons therefore use slots 15-17.
    controls.padconfig[15] = cfg.get_int("controls.padconfig.view1", -1);
    controls.padconfig[16] = cfg.get_int("controls.padconfig.view2", -1);
    controls.padconfig[17] = cfg.get_int("controls.padconfig.view3", -1);

    const std::string encoded =
        cfg.get_string("controls.device_bindings", "");
    const bool default_gamepad_profile =
        cfg.get_int("controls.default_gamepad", 0) != 0;
    const bool parsed_device_bindings =
        parse_device_bindings(encoded, controls.device_bindings);

    if (default_gamepad_profile && !parsed_device_bindings)
    {
        // A freshly generated config starts with a standard SDL/XInput pad
        // profile. Keep it in the legacy arrays until SDL is initialized; the
        // first normal config save converts it to persistent device bindings.
        controls.device_bindings.clear();
        apply_default_gamepad_legacy_bindings(controls);
    }
    else
    {
        if (!parsed_device_bindings)
            migrate_legacy_device_bindings(controls);

        disable_migrated_legacy_bindings(controls);
    }

    // load_core() may already have created config.xml using its historical
    // defaults. Rewrite it now with the canonical first-run profile above.
    if (first_run)
        save();
}

bool Config::save()
{
    cfg.put_int(
        "video.pixel_scaler",
        pixel_scaler::mode.load(std::memory_order_relaxed));
    cfg.put_int(
        "video.pixel_scaler_last",
        pixel_scaler::last_mode.load(std::memory_order_relaxed));

    // Keep the on/off state separate so switching rumble off never overwrites
    // the user's preferred intensity.
    cfg.put_int(
        "controls.rumble_enabled",
        gamepad_rumble::enabled ? 1 : 0);

    // Add the direct-view keyboard bindings to the same config tree before the
    // existing save routine writes it.
    cfg.put_int("controls.keyconfig.view1", controls.keyconfig[12]);
    cfg.put_int("controls.keyconfig.view2", controls.keyconfig[13]);
    cfg.put_int("controls.keyconfig.view3", controls.keyconfig[14]);

    // Once SDL is running, turn the first-run generic Xbox profile into the
    // same physical-device bindings used by the binding matrix. Existing user
    // assignments win target-by-target; missing cells receive the defaults.
    if (cfg.get_int("controls.default_gamepad", 0) != 0 &&
        materialize_default_gamepad_bindings(controls))
    {
        disable_migrated_legacy_bindings(controls);
        cfg.erase("controls.default_gamepad");
    }

    // Per-device bindings supersede the old single pad/axis assignment for all
    // controls represented by the matrix.
    cfg.put_string(
        "controls.device_bindings",
        encode_device_bindings(controls.device_bindings));

    // Always materialize the current DX wheel-tuning defaults in the generated
    // config, even when haptics are disabled on first launch. Then attach the
    // English documentation directly to the real XML tree that save_core()
    // writes to config.xml.
    seed_ffb_tuning_defaults();
    add_ffb_config_comments(cfg);

    // A colour changed in the frontend settings menu is a real default change.
    // While the engine is running, car_pal is runtime state: Music Select and
    // the race are never allowed to promote that temporary value implicitly.
    if (cannonball::state != cannonball::STATE_GAME)
        car_palette_state::set_default(engine.car_pal);

    // save_core() persists engine.car_pal as engine.car_color. Temporarily
    // substitute the persistent attract/default colour so a Music Select or
    // in-race colour can never leak into config.xml.
    const int runtime_car_pal = engine.car_pal;
    engine.car_pal = car_palette_state::get_default(runtime_car_pal);
    const bool saved = save_core();
    engine.car_pal = runtime_car_pal;

    return saved;
}

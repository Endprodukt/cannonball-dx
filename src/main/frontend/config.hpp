/***************************************************************************
    XML Configuration File Handling - CannonBall DX.

    Load Settings.
    Load & Save Hi-Scores.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include <SDL.h>
#include <set>
#include <string>
#include <vector>
#include "stdint.hpp"
#include "highscore_storage.hpp" // one physical highscores.xml for all score tables

struct data_settings_t
{
    std::string rom_path;
    std::string res_path;
    std::string save_path;
    std::string cfg_file;
    int crc32;

    std::string file_scores;            // Arcade Hi-Scores (World & Japanese)
    std::string file_scores_jap;
    std::string file_ttrial;            // Time Trial Hi-Scores
    std::string file_ttrial_jap;
    std::string file_cont;              // Continous Mode Hi-Scores
    std::string file_cont_jap;
    std::string file_stats;             // Machine stats (currently number of games played and minutes run time)
};

struct stats_t
{
    // arcade machine stats
    // these do what mechanical counters would have done on real arcade hardware
    // saved every minute
    int playcount;	  // Games played
    int runtime;          // Total run time in minutes
};

struct music_t
{
    // Audio Format
    const static int IS_YM_INT = 0; // Intenal YM Track (from OutRun ROMs)
    const static int IS_YM_EXT = 1; // External YM Track (from Binary)
    const static int IS_WAV = 2;    // External WAV Track
    int type;

    int cmd;                        // Z80 Command
    std::string title;
    std::string filename;
};

struct ttrial_settings_t
{
    int laps;
    int traffic;
    uint16_t best_times[15];
};

struct menu_settings_t
{
    int enabled;
    int road_scroll_speed;
};

struct video_settings_t
{
    const static int MODE_WINDOW  = 0;
    const static int MODE_FULL    = 1;
    const static int MODE_STRETCH = 2;

    int mode;
    int scale;
    int scanlines;
    int widescreen;
    int fps;
    int fps_count;
    int hires;
    int hires_next;
    int filtering;
    int vsync;
    int shadow;

    // JJP - User configurable X and Y position
    // range is -100 to +100, being pixel offsets from calculated image position
    int x_offset;
    int y_offset;

    // JJP - Blargg CRT filtering constants
    const static int BLARGG_DISABLE   = 0;
    const static int BLARGG_COMPOSITE = 1;
    const static int BLARGG_SVIDEO    = 2;
    const static int BLARGG_RGB       = 3;

    // JJP - Blargg filtering settings
    int blargg;            // Blargg mode - per above constants
    int saturation;
    int contrast;
    int brightness;
    int sharpness;
    int gamma;
    int hue;
    int resolution;

    // JJP - Shader constants
    const static int SHADOW_MASK_OFF     = 0;
    const static int SHADOW_MASK_OVERLAY = 1;
    const static int SHADOW_MASK_SHADER  = 2;

    const static int SHADER_OFF          = 0;
    const static int SHADER_FAST         = 1;
    const static int SHADER_FULL         = 2;

    // JJP - CRT Shader settings
    int shader_mode;        // 0 = off (actually pass-through), 1 = fast, 2 = full
    int shadow_mask;        // 0 = off, 1 = overlay based (fast), 2 = shader based (looks better)
    int mask_size;          // 1 = normal, 2 for high DPI screens eg retina
    int crt_shape;          // actually implemented as an SDL2 texture overlay
    int vignette;           // implemented in overlay or shader
    int noise;
    int warpX;
    int warpY;
    int maskDim;            // 0-95, will be used as [value]/100
    int maskBoost;          // 100-195, will be used [value]/100
    int desaturate;
    int desaturate_edges;
    int brightboost;
    int hiresprites;        // 0 = original; 1 = hires mode
};

struct sound_settings_t
{
    int enabled;
    int rate;
    int advertise;
    int preview;
    int fix_samples;
    int music_timer;
    std::vector <music_t> music;
    int callback_rate;   // 0 = 8ms, 1 = 16ms (needed for WSL2)
    int playback_device; // omit from config file or set to -1 to use system default
    int wave_volume;     // when using .wav files, the playback volume (1-8 where 5 = no adjustment)
    int custom_tracks_loaded = 0; // used to mask help text at startup if tracks are loaded
};

// A physical-device binding used by the new control-binding editor. Keyboard
// bindings continue to use keyconfig[]; these entries allow any number of SDL
// devices (wheel, gamepad, shifter, etc.) to remain configured in parallel.
struct device_binding_t
{
    enum Type
    {
        TYPE_BUTTON = 0,
        TYPE_AXIS   = 1,
        TYPE_HAT    = 2,
    };

    enum Target
    {
        TARGET_STEER = 0,
        TARGET_ACCEL,
        TARGET_BRAKE,
        TARGET_GEAR1,
        TARGET_GEAR2,
        TARGET_START,
        TARGET_COIN,
        TARGET_MENU,
        TARGET_VIEW,
        TARGET_VIEW1,
        TARGET_VIEW2,
        TARGET_VIEW3,
    };

    int target = TARGET_STEER;
    int type = TYPE_BUTTON;
    int index = -1;
    int value = 0;              // HAT direction; unused for buttons/axes
    std::string device;         // persistent SDL device signature, or "*" for legacy any-device binding
};

struct controls_settings_t
{
    const static int GEAR_BUTTON   = 0;
    const static int GEAR_PRESS    = 1; // For cabinets
    const static int GEAR_SEPARATE = 2; // Separate button presses
    const static int GEAR_AUTO     = 3;

    int gear;
    int steer_speed;   // Steering Digital Speed
    int pedal_speed;   // Pedal Digital Speed
    int padconfig[18]; // Legacy Joypad Button Config (15-17 = direct view buttons)
    int keyconfig[15]; // Keyboard Button Config (12-14 = direct view buttons)
    int pad_id;        // Use the N'th joystick on the system.
    int analog;        // Use analog controls
    int axis[4];       // Legacy Analog Axis
    std::string axis_device[4]; // Persistent device signature for each legacy analog axis
    int asettings[2];  // Analog Settings
    bool invert[3];    // Invert Analog Axis

    // Persistent per-device bindings used by the binding matrix. This is kept
    // alongside the legacy arrays so old configuration files still load.
    std::vector<device_binding_t> device_bindings;

    // Custom HAT bindings for UP, DOWN, LEFT, RIGHT
    int hat[4];
    int hat_value[4];
    std::string hat_device[4];

    int direction_custom[4];

    float rumble;              // Simple Controller Rumble Support
    int haptic;                // Force Feedback Enabled
    int ffb_strength;          // FFB Effects Strength 0-100%
    int centering_strength;    // Native Centering Spring Strength 0-100%
    int max_force;
    int min_force;
    int force_duration;
};

struct smartypi_settings_t
{
    int enabled;      // CannonBall used in conjunction with SMARTYPI in arcade cabinet
    int ouputs;       // Write Digital Outputs to console
    int cabinet;      // Cabinet Type
};

struct engine_settings_t
{
    int dip_time;
    int dip_traffic;
    bool freeplay;
    bool freeze_timer;
    bool disable_traffic;
    int jap;
    int prototype;
    int randomgen;
    int level_objects;
    bool fix_bugs;
    bool fix_bugs_backup;
    bool fix_timer;
    bool layout_debug;
    bool hiscore_delete;  // Allow deletion of last entry in score table
    int hiscore_timer;    // Override default timer on high-score entry screen
    int new_attract;      // New Attract Mode
    bool grippy_tyres;    // Handling: Stick to track
    bool offroad;         // Handling: Drive off-road
    bool bumper;          // Handling: Smash into other cars without spinning
    bool turbo;           // Handling: Faster Car
    int car_pal;          // Car Palette
};

class Config
{
public:
    data_settings_t        data;
    stats_t                stats;
    menu_settings_t        menu;
    video_settings_t       video;
    sound_settings_t       sound;
    controls_settings_t    controls;
    engine_settings_t      engine;
    ttrial_settings_t      ttrial;
    smartypi_settings_t    smartypi;

    int master_break_key = SDLK_ESCAPE;

    const static int CABINET_MOVING  = 0;
    const static int CABINET_UPRIGHT = 1;
    const static int CABINET_MINI    = 2;

    // Active driving-input family. Keyboard controls remain available in both
    // modes, while physical GAMEPAD/WHEEL bindings and their feedback outputs
    // are mutually exclusive at runtime.
    const static int INPUT_GAMEPAD = 0;
    const static int INPUT_WHEEL   = 1;

    // Internal screen width and height
    uint16_t s16_width, s16_height;

    // Internal screen x offset
    uint16_t s16_x_off;

    // 30 or 60 fps
    int fps;

    // Original game ticks sprites at 30fps but background scroll at 60fps
    int tick_fps;

    // Continuous Mode: Traffic Setting
    int cont_traffic;

    Config(void);
    ~Config(void);

    void get_custom_music(const std::string& respath);
    void set_config_file(const std::string& filename);
    void load();
    bool save();
    void load_scores(bool original_mode);
    void save_scores(bool original_mode);
    void load_stats();
    void save_stats();
    void load_timetrial_scores();
    void save_timetrial_scores();
    bool clear_scores();
    void set_fps(int fps);
    void inc_time();
    void inc_traffic();

    int input_mode()
    {
        const int stored = cfg.get_int("controls.input_mode", -1);
        if (stored == INPUT_GAMEPAD || stored == INPUT_WHEEL)
            return stored;

        // Migration for configs created before INPUT MODE existed. Existing
        // wheel users generally already have a W: binding; otherwise prefer
        // GAMEPAD so a standard controller remains immediately usable.
        for (const auto& binding : controls.device_bindings)
        {
            if (binding.device.size() >= 2 &&
                binding.device.compare(0, 2, "W:") == 0)
            {
                return INPUT_WHEEL;
            }
        }

        return INPUT_GAMEPAD;
    }

    bool input_mode_is_gamepad()
    {
        return input_mode() == INPUT_GAMEPAD;
    }

    bool input_mode_is_wheel()
    {
        return input_mode() == INPUT_WHEEL;
    }

    void set_input_mode(int mode)
    {
        if (mode != INPUT_WHEEL)
            mode = INPUT_GAMEPAD;

        cfg.put_int("controls.input_mode", mode);
    }

    void cycle_input_mode()
    {
        set_input_mode(
            input_mode() == INPUT_WHEEL
                ? INPUT_GAMEPAD
                : INPUT_WHEEL);
    }

    // Per-effect wheel FFB tuning lives directly in config.xml. Effect and
    // spring strength values use a clear 0..100 range. The two spring speed
    // thresholds use the game's vehicle-speed units instead (0..294).
    int ffb_effect_setting(const char* name, int default_value)
    {
        const std::string setting = name ? name : "";
        int canonical_default = default_value;

        if      (setting == "sand")                     canonical_default = 3;
        else if (setting == "tyre_slip")                canonical_default = 11;
        else if (setting == "offroad_rumble_one_wheel") canonical_default = 20;
        else if (setting == "offroad_rumble_full")      canonical_default = 30;
        else if (setting == "offroad_pull_one_wheel")   canonical_default = 35;
        else if (setting == "offroad_pull_full")        canonical_default = 21;
        else if (setting == "gear_shift")               canonical_default = 49;
        else if (setting == "music_selector")           canonical_default = 7;
        else if (setting == "traffic_skid")             canonical_default = 70;
        else if (setting == "crash_bump")               canonical_default = 70;
        else if (setting == "crash_spin_impact")        canonical_default = 70;
        else if (setting == "crash_spin")               canonical_default = 70;
        else if (setting == "crash_flip_impact")        canonical_default = 70;
        else if (setting == "crash_flip")               canonical_default = 70;
        else if (setting == "crash_flip_landing")       canonical_default = 70;
        else if (setting == "start_steering")           canonical_default = 70;
        else if (setting == "start_rev_shake")          canonical_default = 30;

        const std::string path =
            std::string("controls.analog.haptic.effects.") + setting;
        int value = cfg.get_int(path, canonical_default);
        if (value < 0)
            return 0;
        if (value > 100)
            return 100;
        return value;
    }

    void set_ffb_effect_setting(const char* name, int value)
    {
        const std::string setting = name ? name : "";
        if (value < 0)
            value = 0;
        else if (value > 100)
            value = 100;

        cfg.put_int(
            std::string("controls.analog.haptic.effects.") + setting,
            value);
    }

    int ffb_spring_setting(const char* name, int default_value)
    {
        const std::string setting = name ? name : "";
        int canonical_default = default_value;

        if      (setting == "low_speed")             canonical_default = 28;
        else if (setting == "high_speed")            canonical_default = 70;
        else if (setting == "sliding")               canonical_default = 47;
        else if (setting == "speed_start")           canonical_default = 100;
        else if (setting == "speed_full")            canonical_default = 240;
        else if (setting == "traffic_skid")          canonical_default = 35;
        else if (setting == "crash_bump")            canonical_default = 46;
        else if (setting == "crash_spin")            canonical_default = 25;
        else if (setting == "crash_recovery")        canonical_default = 49;
        else if (setting == "crash_flip_start")      canonical_default = 32;
        else if (setting == "crash_flip_airborne")   canonical_default = 7;
        else if (setting == "crash_flip_transition") canonical_default = 18;
        else if (setting == "crash_flip_landing")    canonical_default = 32;
        else if (setting == "crash_flip_recovery")   canonical_default = 49;

        const std::string path =
            std::string("controls.analog.haptic.spring.") + setting;
        int value = cfg.get_int(path, canonical_default);
        const int maximum =
            (setting == "speed_start" || setting == "speed_full") ? 294 : 100;

        if (value < 0)
            return 0;
        if (value > maximum)
            return maximum;
        return value;
    }

    void set_ffb_spring_setting(const char* name, int value)
    {
        const std::string setting = name ? name : "";
        const int maximum =
            (setting == "speed_start" || setting == "speed_full") ? 294 : 100;

        if (value < 0)
            value = 0;
        else if (value > maximum)
            value = maximum;

        cfg.put_int(
            std::string("controls.analog.haptic.spring.") + setting,
            value);
    }

    // Seed missing FFB tuning entries into the in-memory XML tree when the
    // wheel backend starts. Existing config files therefore keep their values,
    // while missing entries receive the tested DX headroom preset.
    void seed_ffb_tuning_defaults()
    {
        if (cfg.get_int("controls.analog.haptic.<xmlattr>.enabled", -1) < 0)
        {
            controls.haptic = 1;
            cfg.put_int("controls.analog.haptic.<xmlattr>.enabled", 1);
        }

        if (cfg.get_int("controls.analog.haptic.strength", -1) < 0)
        {
            controls.ffb_strength = 50;
            cfg.put_int("controls.analog.haptic.strength", 50);
        }

        if (cfg.get_int("controls.analog.haptic.centering_strength", -1) < 0)
        {
            controls.centering_strength = 60;
            cfg.put_int("controls.analog.haptic.centering_strength", 60);
        }

        auto seed_effect = [&](const char* name, int default_value)
        {
            const std::string path =
                std::string("controls.analog.haptic.effects.") + name;
            cfg.put_int(path, ffb_effect_setting(name, default_value));
        };

        auto seed_spring = [&](const char* name, int default_value)
        {
            const std::string path =
                std::string("controls.analog.haptic.spring.") + name;
            cfg.put_int(path, ffb_spring_setting(name, default_value));
        };

        seed_effect("sand", 3);
        seed_effect("tyre_slip", 11);
        seed_effect("offroad_rumble_one_wheel", 20);
        seed_effect("offroad_rumble_full", 30);
        seed_effect("offroad_pull_one_wheel", 35);
        seed_effect("offroad_pull_full", 21);
        seed_effect("gear_shift", 49);
        seed_effect("music_selector", 7);
        seed_effect("traffic_skid", 70);
        seed_effect("crash_bump", 70);
        seed_effect("crash_spin_impact", 70);
        seed_effect("crash_spin", 70);
        seed_effect("crash_flip_impact", 70);
        seed_effect("crash_flip", 70);
        seed_effect("crash_flip_landing", 70);
        seed_effect("start_steering", 70);
        seed_effect("start_rev_shake", 30);

        seed_spring("low_speed", 28);
        seed_spring("high_speed", 70);
        seed_spring("sliding", 47);
        seed_spring("speed_start", 100);
        seed_spring("speed_full", 240);
        seed_spring("traffic_skid", 35);
        seed_spring("crash_bump", 46);
        seed_spring("crash_spin", 25);
        seed_spring("crash_recovery", 49);
        seed_spring("crash_flip_start", 32);
        seed_spring("crash_flip_airborne", 7);
        seed_spring("crash_flip_transition", 18);
        seed_spring("crash_flip_landing", 32);
        seed_spring("crash_flip_recovery", 49);
    }

    // Shared Music Select / Time Trial selector duration. 0 disables the
    // automatic selection, otherwise the supported values are 15 or 30 seconds.
    // The previous ON/OFF implementation stored 1 for ON; treat that legacy
    // value as the new 30-second default so existing DX configs migrate cleanly.
    int selection_timer_seconds()
    {
        const int value = cfg.get_int("engine.selection_timers", 30);
        if (value == 1)
            return 30;
        if (value == 15 || value == 30)
            return value;
        return 0;
    }

    bool selection_timers_enabled()
    {
        return selection_timer_seconds() != 0;
    }

    void set_selection_timer_seconds(int seconds)
    {
        if (seconds != 15 && seconds != 30)
            seconds = 0;
        cfg.put_int("engine.selection_timers", seconds);
    }

    void cycle_selection_timer()
    {
        const int seconds = selection_timer_seconds();
        if (seconds == 15)
            set_selection_timer_seconds(30);
        else if (seconds == 30)
            set_selection_timer_seconds(0);
        else
            set_selection_timer_seconds(15);
    }

    // To support multi-threaded SDL module:
    bool videoRestartRequired = false;

    static constexpr int BUMPER_VIEW_HEIGHT_LEVELS = 5;
    static constexpr int ENGINE_VIBRATION_DEFAULT_STRENGTH = 4;
    static constexpr int ENGINE_PERIOD_DEFAULT_MS = 110;
    static constexpr int ENGINE_PERIOD_MIN_MS = 10;
    static constexpr int ENGINE_PERIOD_MAX_MS = 500;
    static constexpr int ENDLESS_DEFAULT_START_TIME = 80;
    static constexpr int ENDLESS_DEFAULT_CHECKPOINT_TIME = 55;
    static constexpr int ENDLESS_DEFAULT_TIME_DECREASE = 2;
    static constexpr int ENDLESS_DEFAULT_TIME_INTERVAL = 3;
    static constexpr int ENDLESS_DEFAULT_MIN_CHECKPOINT = 30;
    static constexpr int ENDLESS_DEFAULT_START_TRAFFIC = 2;
    static constexpr int ENDLESS_DEFAULT_TRAFFIC_INCREASE = 1;
    static constexpr int ENDLESS_DEFAULT_TRAFFIC_INTERVAL = 3;
    static constexpr int ENDLESS_DEFAULT_MAX_TRAFFIC = 8;
    static constexpr int ENDLESS_DEFAULT_RANDOM_START = 0;
    static constexpr int SYSTEM_ACTION_PAUSE = 0;
    static constexpr int SYSTEM_ACTION_ACCEPT = 1;
    static constexpr int SYSTEM_ACTION_BACK = 2;
    const char* system_action_name(int action)
    {
        if (action == SYSTEM_ACTION_ACCEPT) return "accept";
        if (action == SYSTEM_ACTION_BACK) return "back";
        return "pause";
    }
    int system_action_key(int action)
    {
        const std::string path = std::string("controls.system.") + system_action_name(action) + ".keyboard";
        return cfg.get_int(path, -1);
    }
    void set_system_action_key(int action, int key)
    {
        const std::string path = std::string("controls.system.") + system_action_name(action) + ".keyboard";
        cfg.put_int(path, key);
    }
    std::string system_action_group_path(int action, int group, const char* leaf)
    {
        return std::string("controls.system.") + system_action_name(action) +
            (group == 0 ? ".gamepad." : ".wheel.") + leaf;
    }
    int system_action_binding_type(int action, int group)
    {
        return cfg.get_int(system_action_group_path(action, group, "type"), -1);
    }
    int system_action_binding_index(int action, int group)
    {
        return cfg.get_int(system_action_group_path(action, group, "index"), -1);
    }
    int system_action_binding_value(int action, int group)
    {
        return cfg.get_int(system_action_group_path(action, group, "value"), 0);
    }
    std::string system_action_binding_device(int action, int group)
    {
        return cfg.get_string(system_action_group_path(action, group, "device"), "");
    }
    void set_system_action_binding(int action, int group, int type, int index, int value, const std::string& device)
    {
        cfg.put_int(system_action_group_path(action, group, "type"), type);
        cfg.put_int(system_action_group_path(action, group, "index"), index);
        cfg.put_int(system_action_group_path(action, group, "value"), value);
        cfg.put_string(system_action_group_path(action, group, "device"), device);
    }
    void clear_system_action_binding(int action, int group)
    {
        set_system_action_binding(action, group, -1, -1, 0, "!");
    }
    int radio_key()
    {
        return cfg.get_int("controls.radio.keyboard", -1);
    }
    void set_radio_key(int key)
    {
        cfg.put_int("controls.radio.keyboard", key);
    }
    int radio_binding_type(int group)
    {
        return cfg.get_int(
            group == 0 ? "controls.radio.gamepad.type" : "controls.radio.wheel.type",
            -1);
    }
    int radio_binding_index(int group)
    {
        return cfg.get_int(
            group == 0 ? "controls.radio.gamepad.index" : "controls.radio.wheel.index",
            -1);
    }
    int radio_binding_value(int group)
    {
        return cfg.get_int(
            group == 0 ? "controls.radio.gamepad.value" : "controls.radio.wheel.value",
            0);
    }
    std::string radio_binding_device(int group)
    {
        return cfg.get_string(
            group == 0 ? "controls.radio.gamepad.device" : "controls.radio.wheel.device",
            "");
    }
    void set_radio_binding(int group, int type, int index, int value, const std::string& device)
    {
        const char* type_path = group == 0 ? "controls.radio.gamepad.type" : "controls.radio.wheel.type";
        const char* index_path = group == 0 ? "controls.radio.gamepad.index" : "controls.radio.wheel.index";
        const char* value_path = group == 0 ? "controls.radio.gamepad.value" : "controls.radio.wheel.value";
        const char* device_path = group == 0 ? "controls.radio.gamepad.device" : "controls.radio.wheel.device";
        cfg.put_int(type_path, type);
        cfg.put_int(index_path, index);
        cfg.put_int(value_path, value);
        cfg.put_string(device_path, device);
    }
    void clear_radio_binding(int group)
    {
        set_radio_binding(group, -1, -1, 0, "!");
    }
    int engine_vibration_strength()
    {
        int value = cfg.get_int(
            "controls.analog.haptic.engine_vibration_strength",
            ENGINE_VIBRATION_DEFAULT_STRENGTH);
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        return value;
    }
    void set_engine_vibration_strength(int value)
    {
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        cfg.put_int("controls.analog.haptic.engine_vibration_strength", value);
    }
    int engine_period_ms()
    {
        int value = cfg.get_int(
            "controls.analog.haptic.engine_period_ms",
            ENGINE_PERIOD_DEFAULT_MS);
        if (value < ENGINE_PERIOD_MIN_MS) value = ENGINE_PERIOD_MIN_MS;
        if (value > ENGINE_PERIOD_MAX_MS) value = ENGINE_PERIOD_MAX_MS;
        return value;
    }
    void set_engine_period_ms(int value)
    {
        const int current = engine_period_ms();
        /* The existing FFB menu supplies +/-5 for this physical timing value.
           Promote those menu nudges to the requested 10 ms steps while still
           allowing exact values loaded or written directly through config. */
        if (value == current - 5) value = current - 10;
        else if (value == current + 5) value = current + 10;
        if (value < ENGINE_PERIOD_MIN_MS) value = ENGINE_PERIOD_MIN_MS;
        if (value > ENGINE_PERIOD_MAX_MS) value = ENGINE_PERIOD_MAX_MS;
        cfg.put_int("controls.analog.haptic.engine_period_ms", value);
    }
    int bumper_view_height_level()
    {
        int level = cfg.get_int("engine.bumper_view_height", 1);
        if (level < 0 || level >= BUMPER_VIEW_HEIGHT_LEVELS)
            level = 1;
        return level;
    }
    void set_bumper_view_height_level(int level)
    {
        if (level < 0)
            level = 0;
        else if (level >= BUMPER_VIEW_HEIGHT_LEVELS)
            level = BUMPER_VIEW_HEIGHT_LEVELS - 1;
        cfg.put_int("engine.bumper_view_height", level);
    }
    void cycle_bumper_view_height()
    {
        set_bumper_view_height_level(
            (bumper_view_height_level() + 1) % BUMPER_VIEW_HEIGHT_LEVELS);
    }
    bool ferrari_mirror_fix()
    {
        return cfg.get_int("engine.ferrari_mirror_fix", 1) != 0;
    }
    void set_ferrari_mirror_fix(bool enabled)
    {
        cfg.put_int("engine.ferrari_mirror_fix", enabled ? 1 : 0);
    }
    void toggle_ferrari_mirror_fix()
    {
        set_ferrari_mirror_fix(!ferrari_mirror_fix());
    }
    bool bugfix_read_setting(const char* name, bool fallback)
    {
        const std::string path = std::string("engine.bugfixes.") + name;
        return cfg.get_int(path, fallback ? 1 : 0) != 0;
    }
    void bugfix_write_setting(const char* name, bool enabled)
    {
        cfg.put_int(std::string("engine.bugfixes.") + name, enabled ? 1 : 0);
    }
    bool bugfix_steering_input()
    {
        return bugfix_read_setting("steering_input", true);
    }
    bool bugfix_checkpoint_lap_time()
    {
        return bugfix_read_setting("checkpoint_lap_time", true);
    }
    bool bugfix_ending_palette()
    {
        return bugfix_read_setting("ending_palette", true);
    }
    bool bugfix_music_select_tile()
    {
        return bugfix_read_setting("music_select_tile", true);
    }
    bool bugfix_menu_map_road_line()
    {
        return bugfix_read_setting("menu_map_road_line", true);
    }
    bool bugfix_crash_engine_sound()
    {
        return bugfix_read_setting("crash_engine_sound", true);
    }
    bool bugfix_wheel_slip_se()
    {
        /* false keeps the original arcade/MAME slip detection by default. */
        return bugfix_read_setting("wheel_slip_se", false);
    }
    void set_bugfix_steering_input(bool enabled)
    {
        bugfix_write_setting("steering_input", enabled);
    }
    void set_bugfix_checkpoint_lap_time(bool enabled)
    {
        bugfix_write_setting("checkpoint_lap_time", enabled);
    }
    void set_bugfix_ending_palette(bool enabled)
    {
        bugfix_write_setting("ending_palette", enabled);
    }
    void set_bugfix_music_select_tile(bool enabled)
    {
        bugfix_write_setting("music_select_tile", enabled);
    }
    void set_bugfix_menu_map_road_line(bool enabled)
    {
        bugfix_write_setting("menu_map_road_line", enabled);
    }
    void set_bugfix_crash_engine_sound(bool enabled)
    {
        bugfix_write_setting("crash_engine_sound", enabled);
    }
    void set_bugfix_wheel_slip_se(bool enabled)
    {
        bugfix_write_setting("wheel_slip_se", enabled);
        /* The preserved Ferrari code still branches on this legacy runtime bit. */
        engine.fix_bugs = enabled;
    }
    void sync_bugfix_runtime()
    {
        /* Only Ferrari slip detection still consumes the legacy aggregate flag. */
        engine.fix_bugs = bugfix_wheel_slip_se();
    }
    void reset_bugfix_settings()
    {
        set_bugfix_steering_input(true);
        set_bugfix_checkpoint_lap_time(true);
        set_bugfix_ending_palette(true);
        set_bugfix_music_select_tile(true);
        set_bugfix_menu_map_road_line(true);
        set_bugfix_crash_engine_sound(true);
        set_bugfix_wheel_slip_se(false);
    }
    int endless_read_setting(const char* name, int fallback, int minimum, int maximum)
    {
        const std::string path = std::string("endless.") + name;
        int value = cfg.get_int(path, fallback);
        if (value < minimum) value = minimum;
        if (value > maximum) value = maximum;
        return value;
    }
    void endless_write_setting(const char* name, int value, int minimum, int maximum)
    {
        if (value < minimum) value = minimum;
        if (value > maximum) value = maximum;
        cfg.put_int(std::string("endless.") + name, value);
    }
    bool endless_random_start()
    {
        return endless_read_setting("random_start", ENDLESS_DEFAULT_RANDOM_START, 0, 1) != 0;
    }
    void set_endless_random_start(bool enabled)
    {
        endless_write_setting("random_start", enabled ? 1 : 0, 0, 1);
    }
    int endless_start_time()
    {
        return endless_read_setting("start_time", ENDLESS_DEFAULT_START_TIME, 30, 99);
    }
    int endless_checkpoint_time()
    {
        return endless_read_setting("checkpoint_time", ENDLESS_DEFAULT_CHECKPOINT_TIME, 20, 99);
    }
    int endless_time_decrease()
    {
        return endless_read_setting("time_decrease", ENDLESS_DEFAULT_TIME_DECREASE, 0, 10);
    }
    int endless_time_interval()
    {
        return endless_read_setting("time_interval", ENDLESS_DEFAULT_TIME_INTERVAL, 1, 10);
    }
    int endless_min_checkpoint()
    {
        int value = endless_read_setting("min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT, 10, 99);
        const int checkpoint = endless_checkpoint_time();
        if (value > checkpoint) value = checkpoint;
        return value;
    }
    int endless_start_traffic()
    {
        return endless_read_setting("start_traffic", ENDLESS_DEFAULT_START_TRAFFIC, 0, 8);
    }
    int endless_traffic_increase()
    {
        return endless_read_setting("traffic_increase", ENDLESS_DEFAULT_TRAFFIC_INCREASE, 0, 8);
    }
    int endless_traffic_interval()
    {
        return endless_read_setting("traffic_interval", ENDLESS_DEFAULT_TRAFFIC_INTERVAL, 1, 10);
    }
    int endless_max_traffic()
    {
        int value = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8);
        const int start = endless_start_traffic();
        if (value < start) value = start;
        return value;
    }
    void set_endless_start_time(int value)
    {
        endless_write_setting("start_time", value, 30, 99);
    }
    void set_endless_checkpoint_time(int value)
    {
        endless_write_setting("checkpoint_time", value, 20, 99);
        const int checkpoint = endless_checkpoint_time();
        const int stored_min = endless_read_setting("min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT, 10, 99);
        if (stored_min > checkpoint)
            endless_write_setting("min_checkpoint", checkpoint, 10, 99);
    }
    void set_endless_time_decrease(int value)
    {
        endless_write_setting("time_decrease", value, 0, 10);
    }
    void set_endless_time_interval(int value)
    {
        endless_write_setting("time_interval", value, 1, 10);
    }
    void set_endless_min_checkpoint(int value)
    {
        if (value > endless_checkpoint_time()) value = endless_checkpoint_time();
        endless_write_setting("min_checkpoint", value, 10, 99);
    }
    void set_endless_start_traffic(int value)
    {
        endless_write_setting("start_traffic", value, 0, 8);
        const int start = endless_start_traffic();
        const int stored_max = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8);
        if (stored_max < start)
            endless_write_setting("max_traffic", start, 0, 8);
    }
    void set_endless_traffic_increase(int value)
    {
        endless_write_setting("traffic_increase", value, 0, 8);
    }
    void set_endless_traffic_interval(int value)
    {
        endless_write_setting("traffic_interval", value, 1, 10);
    }
    void set_endless_max_traffic(int value)
    {
        endless_write_setting("max_traffic", value, 0, 8);
        const int maximum = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8);
        if (endless_start_traffic() > maximum)
            endless_write_setting("start_traffic", maximum, 0, 8);
    }
    int endless_checkpoint_for_stage(uint16_t stage)
    {
        const int steps = static_cast<int>(stage) / endless_time_interval();
        int value = endless_checkpoint_time() - (steps * endless_time_decrease());
        const int minimum = endless_min_checkpoint();
        if (value < minimum) value = minimum;
        return value;
    }
    int endless_traffic_for_stage(uint16_t stage)
    {
        const int steps = static_cast<int>(stage) / endless_traffic_interval();
        int value = endless_start_traffic() + (steps * endless_traffic_increase());
        const int maximum = endless_max_traffic();
        if (value > maximum) value = maximum;
        return value;
    }
    uint8_t endless_start_time_bcd()
    {
        const int seconds = endless_start_time();
        return static_cast<uint8_t>(((seconds / 10) << 4) | (seconds % 10));
    }
    void reset_endless_settings()
    {
        cfg.put_int("endless.random_start", ENDLESS_DEFAULT_RANDOM_START);
        cfg.put_int("endless.start_time", ENDLESS_DEFAULT_START_TIME);
        cfg.put_int("endless.checkpoint_time", ENDLESS_DEFAULT_CHECKPOINT_TIME);
        cfg.put_int("endless.time_decrease", ENDLESS_DEFAULT_TIME_DECREASE);
        cfg.put_int("endless.time_interval", ENDLESS_DEFAULT_TIME_INTERVAL);
        cfg.put_int("endless.min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT);
        cfg.put_int("endless.start_traffic", ENDLESS_DEFAULT_START_TRAFFIC);
        cfg.put_int("endless.traffic_increase", ENDLESS_DEFAULT_TRAFFIC_INCREASE);
        cfg.put_int("endless.traffic_interval", ENDLESS_DEFAULT_TRAFFIC_INTERVAL);
        cfg.put_int("endless.max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC);
    }


private:
    // Core XML implementation. Public load/save add DX migration and comments.
    void load_core();
    bool save_core();

    xml_parser::ptree cfg;
};

extern Config config;

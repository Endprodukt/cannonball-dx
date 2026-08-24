/***************************************************************************
    XML Configuration File Handling.

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
#include "xml_parser.h" // replaces Boost for XML handling

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
    int ffb_strength;          // FFB Effects Strength 10-100%
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

private:
    // Original config implementation retained by config.cpp wrapper.
    void load_base();
    bool save_base();

    xml_parser::ptree cfg;
};

extern Config config;
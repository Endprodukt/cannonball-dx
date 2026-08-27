/***************************************************************************
    Front End Menu System.

    This file is part of Cannonball.
    Copyright (c) Chris White.
    See license.txt for more details.

    Modifications for CannonBall-SE Copyright (c) 2025, James Pearce
***************************************************************************/

#pragma once

#include <algorithm>
#include <vector>
#include <string>
#include "stdint.hpp"
#include "main.hpp"
#include "roms.hpp"
#include "frontend/ttrial.hpp"
#include "sdl2/input.hpp"
#include "sdl2/pixel_scaler_state.hpp"
#include "sdl2/gamepad_rumble_state.hpp"
#include "engine/audio/osoundint.hpp"
#include "directx/ffeedback.hpp"

class CabDiag;

// Base menu implementation retained from the current CannonBall-SE code.
// Menu derives from this only so the external-output feature can extend the
// unified control-binding wizard without rewriting the rest of the menu.
class MenuBase
{
public:
    MenuBase();
    virtual ~MenuBase(void);

    void populate();
    void init(bool init_main_menu = true);
    void tick();
    void restart_video();

protected:
    CabDiag* cabdiag;

    // Menu state
    uint8_t state;

    enum
    {
        STATE_MENU,
        STATE_REDEFINE_KEYS,
        STATE_REDEFINE_JOY,
        STATE_TTRIAL,
        STATE_DIAGNOSTICS,
    };

    TTrial* ttrial;

    // Music track for music test menu
    int music_track;

    // Redefine keys/joystick substate
    uint8_t redef_state;

    uint32_t frame;

    // Counter for showing messages
    int32_t message_counter;

    // Number of seconds to show message for
    const static int32_t MESSAGE_TIME = 5;

    // Message text
    std::string msg;

    // Cursor
    int16_t cursor;

    struct menu_pair
    {
        int16_t cursor;
        std::vector<std::string>* menu;
    };

    std::vector<menu_pair> menu_stack;

    // Stores whether this is a textual menu (i.e. no options that can be chosen)
    bool is_text_menu;

    // Used to control the horizon pan effect
    uint16_t horizon_pos;

    std::vector<std::string>* menu_selected;
    std::vector<std::string> menu_main;
    std::vector<std::string> menu_gamemodes;
    std::vector<std::string> menu_cont;
    std::vector<std::string> menu_timetrial;
    std::vector<std::string> menu_about;
    std::vector<std::string> menu_settings;
    std::vector<std::string> menu_video;
    // JJP - CRT Emulation related menus
    std::vector<std::string> menu_crt_shader1;
    std::vector<std::string> menu_crt_shader2;
    std::vector<std::string> menu_crt_shape_settings;
    std::vector<std::string> menu_crt_mask_settings;
    std::vector<std::string> menu_blargg_filter;
    // JJP - end of insert
    std::vector<std::string> menu_sound;
    std::vector<std::string> menu_controls;
    std::vector<std::string> menu_controls_gp;
    std::vector<std::string> menu_engine;
    std::vector<std::string> menu_enhancements;
    std::vector<std::string> menu_handling;
    std::vector<std::string> menu_musictest;
    std::vector<std::string> menu_s_exsettings;     // smartypi specific
    std::vector<std::string> menu_s_tests;          // smartypi specific
    std::vector<std::string> menu_s_dips;           // smartypi specific
    std::vector<std::string> menu_s_enhance;        // smartypi specific

    std::vector<std::string> text_redefine;

    void populate_for_pc();
    virtual void populate_controls();
    void populate_for_cabinet();
    void tick_ui();
    void draw_menu_options();
    void draw_text(std::string);
    virtual void tick_menu();
    virtual bool select_pressed();
    void set_menu(std::vector<std::string>*);
    void menu_back();
    void refresh_menu();
    void set_menu_text(std::string s1, std::string s2);
    void redefine_keyboard();
    virtual void redefine_joystick();
    void display_message(std::string);
    bool check_jap_roms();
    void start_game(int mode, int settings = 0);
};

class Menu : public MenuBase
{
public:
    Menu() = default;
    ~Menu() override = default;

    // Populate the preserved SE menus first, then reshape the PC frontend into
    // the shallower DX hierarchy. Settings are saved immediately, so there is
    // no explicit SAVE action.
    void populate()
    {
        MenuBase::populate();

        auto remove_save_entry = [](std::vector<std::string>& entries)
        {
            for (auto it = entries.begin(); it != entries.end(); )
            {
                if (it->rfind("SAVE AND RETURN", 0) == 0)
                    it = entries.erase(it);
                else
                    ++it;
            }
        };

        remove_save_entry(menu_settings);
        remove_save_entry(menu_s_dips);
        remove_save_entry(menu_s_exsettings);

        if (!menu_about.empty())
            menu_about[0] = std::string("CANNONBALL DX ") + CANNONBALL_DX_VERSION;

        // Keep the inherited CannonBall-SE credit and add the DX fork credit above it.
        if (menu_about.size() >= 6)
            menu_about.insert(menu_about.begin() + 5, "DX BUILD COPYRIGHT 2026 ENDPRODUKT");

        for (auto it = menu_cont.begin(); it != menu_cont.end(); )
        {
            if (it->rfind("TRAFFIC ", 0) == 0)
                it = menu_cont.erase(it);
            else
                ++it;
        }

        const int selection_seconds = config.selection_timer_seconds();
        const std::string selection_timer_entry =
            std::string("SELECTION TIMER ") +
            (selection_seconds == 0
                ? "OFF"
                : std::to_string(selection_seconds) + " SEC");

        const int scaler_mode =
            pixel_scaler::mode.load(std::memory_order_relaxed);
        const std::string scaler_entry =
            std::string("PIXEL SCALER ") + pixel_scaler::name(scaler_mode);

        // The real game modes are selected on Music Select. The old SE Game
        // Modes page and its Enhanced/Original presets are therefore removed
        // from the visible PC frontend rather than duplicated here.
        if (!config.smartypi.enabled)
        {
            menu_main.clear();
            menu_main.push_back(ENTRY_PLAYGAME);
            menu_main.push_back(ENTRY_SETTINGS);
            menu_main.push_back(ENTRY_ABOUT);
            menu_main.push_back(ENTRY_EXIT);

            menu_settings.clear();
            menu_settings.push_back("CONTROLS");
            menu_settings.push_back("VIDEO");
#ifdef COMPILE_SOUND_CODE
            menu_settings.push_back("AUDIO");
#endif
            menu_settings.push_back("GAMEPLAY");
            menu_settings.push_back("ENHANCEMENTS");
            menu_settings.push_back("SYSTEM");
            menu_settings.push_back(ENTRY_BACK);

            menu_video.clear();
            menu_video.push_back(ENTRY_FULLSCREEN);
            menu_video.push_back(ENTRY_WIDESCREEN);
            menu_video.push_back(scaler_entry);
            menu_video.push_back(ENTRY_FPS_COUNTER);
            menu_video.push_back(ENTRY_X_OFFSET);
            menu_video.push_back(ENTRY_Y_OFFSET);
            menu_video.push_back(ENTRY_CRT_SHADER1);
            menu_video.push_back(ENTRY_BLARGG_FILTER);
            menu_video.push_back(ENTRY_BACK);

            menu_engine.clear();
            menu_engine.push_back(ENTRY_TIME);
            menu_engine.push_back(ENTRY_TRAFFIC);
            menu_engine.push_back(ENTRY_FREEPLAY);
            menu_engine.push_back(selection_timer_entry);
            menu_engine.push_back(ENTRY_SUB_HANDLING);
            menu_engine.push_back(ENTRY_BACK);

            menu_enhancements.clear();
            menu_enhancements.push_back(ENTRY_HIRES);
            menu_enhancements.push_back(ENTRY_SPRITERES);
            menu_enhancements.push_back(ENTRY_ATTRACT);
            menu_enhancements.push_back(ENTRY_OBJECTS);
            menu_enhancements.push_back(ENTRY_PROTOTYPE);
            menu_enhancements.push_back(ENTRY_TIMER);
            menu_enhancements.push_back(ENTRY_BACK);
        }
    }

    // Wrapper hook used to keep analog steering from moving normal menu
    // cursors while leaving in-game steering untouched.
    void tick();

    // Escape is reserved as frontend BACK. The main event loop calls this
    // instead of treating Escape as the global master-break key while a menu
    // is active.
    void handle_escape();

    // Enter the existing Time Trial course selector from the in-game music
    // screen. TTrial itself still owns track/lap/traffic setup; once a track is
    // confirmed the normal frontend path starts the engine again.
    void start_time_trial_from_music()
    {
        state = STATE_TTRIAL;
        ttrial->init();
        cannonball::state = cannonball::STATE_MENU;
    }

protected:
    struct FfbMenuItem
    {
        const char* label;
        const char* setting;
    };

    // DX-only submenu for infrequently used administrative options.
    std::vector<std::string> menu_system;

    // FFB tuning pages read their values directly from Config's XML tree.
    std::vector<std::string> menu_ffb_tuning;
    std::vector<std::string> menu_ffb_effects;
    std::vector<std::string> menu_ffb_spring;
    int ffb_effect_page = 0;
    int ffb_spring_page = 0;

    // Directional menu edits use their own tiny save latch. This mirrors the
    // existing Enter-based auto-save path but also waits for renderer restarts
    // before writing values such as hires that are promoted on the next frame.
    bool directional_save_pending = false;

    static std::string ffb_value_text(
        const char* label,
        int value,
        bool percentage = true)
    {
        return std::string(label) + " " + std::to_string(value) +
            (percentage ? "%" : "");
    }

    void populate_ffb_tuning()
    {
        menu_ffb_tuning.clear();
        menu_ffb_tuning.push_back("MAIN EFFECTS");
        menu_ffb_tuning.push_back("SPRING EFFECTS");
        menu_ffb_tuning.push_back(ENTRY_BACK);
    }

    void populate_ffb_effects()
    {
        static const FfbMenuItem ITEMS[] =
        {
            { "SAND",                    "sand" },
            { "TYRE SLIP",               "tyre_slip" },
            { "OFFROAD RUMBLE 1W",       "offroad_rumble_one_wheel" },
            { "OFFROAD RUMBLE FULL",     "offroad_rumble_full" },
            { "OFFROAD PULL 1W",         "offroad_pull_one_wheel" },
            { "OFFROAD PULL FULL",       "offroad_pull_full" },
            { "GEAR SHIFT",              "gear_shift" },
            { "MUSIC SELECTOR",          "music_selector" },
            { "TRAFFIC SKID",            "traffic_skid" },
            { "CRASH BUMP",              "crash_bump" },
            { "CRASH SPIN IMPACT",       "crash_spin_impact" },
            { "CRASH SPIN",              "crash_spin" },
            { "CRASH FLIP IMPACT",       "crash_flip_impact" },
            { "CRASH FLIP",              "crash_flip" },
            { "CRASH FLIP LANDING",      "crash_flip_landing" },
            { "START STEERING",          "start_steering" },
            { "START REV SHAKE",         "start_rev_shake" },
        };

        const int page_size = 9;
        const int item_count = static_cast<int>(sizeof(ITEMS) / sizeof(ITEMS[0]));
        const int first = ffb_effect_page * page_size;
        const int last = std::min(first + page_size, item_count);

        menu_ffb_effects.clear();
        for (int i = first; i < last; i++)
        {
            menu_ffb_effects.push_back(
                ffb_value_text(
                    ITEMS[i].label,
                    config.ffb_effect_setting(ITEMS[i].setting, 0)));
        }

        if (ffb_effect_page == 0)
            menu_ffb_effects.push_back("NEXT PAGE");
        else
            menu_ffb_effects.push_back("PREV PAGE");

        menu_ffb_effects.push_back(ENTRY_BACK);
    }

    void populate_ffb_spring()
    {
        static const FfbMenuItem ITEMS[] =
        {
            { "LOW SPEED",                "low_speed" },
            { "HIGH SPEED",               "high_speed" },
            { "SLIDING",                  "sliding" },
            { "SPEED START",              "speed_start" },
            { "SPEED FULL",               "speed_full" },
            { "TRAFFIC SKID",             "traffic_skid" },
            { "CRASH BUMP",               "crash_bump" },
            { "CRASH SPIN",               "crash_spin" },
            { "CRASH RECOVERY",           "crash_recovery" },
            { "CRASH FLIP START",         "crash_flip_start" },
            { "CRASH FLIP AIRBORNE",      "crash_flip_airborne" },
            { "CRASH FLIP TRANSITION",    "crash_flip_transition" },
            { "CRASH FLIP LANDING",       "crash_flip_landing" },
            { "CRASH FLIP RECOVERY",      "crash_flip_recovery" },
        };

        const int page_size = 7;
        const int item_count = static_cast<int>(sizeof(ITEMS) / sizeof(ITEMS[0]));
        const int first = ffb_spring_page * page_size;
        const int last = std::min(first + page_size, item_count);

        menu_ffb_spring.clear();
        for (int i = first; i < last; i++)
        {
            const bool speed_value =
                std::string(ITEMS[i].setting) == "speed_start" ||
                std::string(ITEMS[i].setting) == "speed_full";

            menu_ffb_spring.push_back(
                ffb_value_text(
                    ITEMS[i].label,
                    config.ffb_spring_setting(ITEMS[i].setting, 0),
                    !speed_value));
        }

        if (ffb_spring_page == 0)
            menu_ffb_spring.push_back("NEXT PAGE");
        else
            menu_ffb_spring.push_back("PREV PAGE");

        menu_ffb_spring.push_back(ENTRY_BACK);
    }

    void adjust_ffb_effect(int delta)
    {
        static const FfbMenuItem ITEMS[] =
        {
            { "SAND",                    "sand" },
            { "TYRE SLIP",               "tyre_slip" },
            { "OFFROAD RUMBLE 1W",       "offroad_rumble_one_wheel" },
            { "OFFROAD RUMBLE FULL",     "offroad_rumble_full" },
            { "OFFROAD PULL 1W",         "offroad_pull_one_wheel" },
            { "OFFROAD PULL FULL",       "offroad_pull_full" },
            { "GEAR SHIFT",              "gear_shift" },
            { "MUSIC SELECTOR",          "music_selector" },
            { "TRAFFIC SKID",            "traffic_skid" },
            { "CRASH BUMP",              "crash_bump" },
            { "CRASH SPIN IMPACT",       "crash_spin_impact" },
            { "CRASH SPIN",              "crash_spin" },
            { "CRASH FLIP IMPACT",       "crash_flip_impact" },
            { "CRASH FLIP",              "crash_flip" },
            { "CRASH FLIP LANDING",      "crash_flip_landing" },
            { "START STEERING",          "start_steering" },
            { "START REV SHAKE",         "start_rev_shake" },
        };

        const int page_size = 9;
        const int item_count = static_cast<int>(sizeof(ITEMS) / sizeof(ITEMS[0]));
        const int index = ffb_effect_page * page_size + cursor;
        if (index < 0 || index >= item_count)
            return;

        int value = config.ffb_effect_setting(ITEMS[index].setting, 0) + delta;
        value = std::max(0, std::min(100, value));
        config.set_ffb_effect_setting(ITEMS[index].setting, value);
        if (!config.save())
            display_message("ERROR SAVING SETTINGS!");
        populate_ffb_effects();
    }

    void adjust_ffb_spring(int delta)
    {
        static const FfbMenuItem ITEMS[] =
        {
            { "LOW SPEED",                "low_speed" },
            { "HIGH SPEED",               "high_speed" },
            { "SLIDING",                  "sliding" },
            { "SPEED START",              "speed_start" },
            { "SPEED FULL",               "speed_full" },
            { "TRAFFIC SKID",             "traffic_skid" },
            { "CRASH BUMP",               "crash_bump" },
            { "CRASH SPIN",               "crash_spin" },
            { "CRASH RECOVERY",           "crash_recovery" },
            { "CRASH FLIP START",         "crash_flip_start" },
            { "CRASH FLIP AIRBORNE",      "crash_flip_airborne" },
            { "CRASH FLIP TRANSITION",    "crash_flip_transition" },
            { "CRASH FLIP LANDING",       "crash_flip_landing" },
            { "CRASH FLIP RECOVERY",      "crash_flip_recovery" },
        };

        const int page_size = 7;
        const int item_count = static_cast<int>(sizeof(ITEMS) / sizeof(ITEMS[0]));
        const int index = ffb_spring_page * page_size + cursor;
        if (index < 0 || index >= item_count)
            return;

        const std::string setting = ITEMS[index].setting;
        const int maximum =
            (setting == "speed_start" || setting == "speed_full") ? 294 : 100;
        int value = config.ffb_spring_setting(ITEMS[index].setting, 0) + delta;
        value = std::max(0, std::min(maximum, value));
        config.set_ffb_spring_setting(ITEMS[index].setting, value);
        if (!config.save())
            display_message("ERROR SAVING SETTINGS!");
        populate_ffb_spring();
    }

    void tick_ffb_tuning_menu()
    {
        if (input.has_pressed(Input::DOWN))
        {
            osoundint.queue_sound(sound::BEEP1);
            if (++cursor >= static_cast<int16_t>(menu_selected->size()))
                cursor = 0;
            return;
        }

        if (input.has_pressed(Input::UP))
        {
            osoundint.queue_sound(sound::BEEP1);
            if (--cursor < 0)
                cursor = static_cast<int16_t>(menu_selected->size()) - 1;
            return;
        }

        if (menu_selected == &menu_ffb_tuning)
        {
            if (!select_pressed())
                return;

            const std::string option = menu_ffb_tuning[cursor];
            if (option == "MAIN EFFECTS")
            {
                ffb_effect_page = 0;
                populate_ffb_effects();
                set_menu(&menu_ffb_effects);
            }
            else if (option == "SPRING EFFECTS")
            {
                ffb_spring_page = 0;
                populate_ffb_spring();
                set_menu(&menu_ffb_spring);
            }
            else if (option.rfind(ENTRY_BACK, 0) == 0)
            {
                menu_back();
            }

            refresh_menu();
            return;
        }

        int delta = 0;
        if (input.has_pressed(Input::LEFT))
            delta = -1;
        else if (input.has_pressed(Input::RIGHT))
            delta = 1;

        const bool selected = delta == 0 && select_pressed();

        if (menu_selected == &menu_ffb_effects)
        {
            const int page_size = 9;
            const int item_count = 17;
            const int visible_items =
                std::min(page_size, item_count - ffb_effect_page * page_size);

            if (cursor < visible_items && (delta != 0 || selected))
            {
                adjust_ffb_effect(delta != 0 ? delta : 5);
                return;
            }

            if (!selected)
                return;

            if (cursor == visible_items)
            {
                ffb_effect_page = ffb_effect_page == 0 ? 1 : 0;
                populate_ffb_effects();
                cursor = 0;
            }
            else
            {
                menu_back();
            }

            refresh_menu();
            return;
        }

        if (menu_selected == &menu_ffb_spring)
        {
            const int page_size = 7;
            const int item_count = 14;
            const int visible_items =
                std::min(page_size, item_count - ffb_spring_page * page_size);

            if (cursor < visible_items && (delta != 0 || selected))
            {
                adjust_ffb_spring(delta != 0 ? delta : 5);
                return;
            }

            if (!selected)
                return;

            if (cursor == visible_items)
            {
                ffb_spring_page = ffb_spring_page == 0 ? 1 : 0;
                populate_ffb_spring();
                cursor = 0;
            }
            else
            {
                menu_back();
            }

            refresh_menu();
        }
    }

    static int wrap_value(int value, int minimum, int maximum, int step, int direction)
    {
        value += step * direction;
        if (value > maximum)
            value = minimum;
        else if (value < minimum)
            value = maximum;
        return value;
    }

    bool adjust_current_value(int direction)
    {
        if (direction == 0 || !menu_selected || cursor < 0 ||
            cursor >= static_cast<int>(menu_selected->size()))
            return false;

        const std::string& option = menu_selected->at(cursor);
        auto selected = [&](const char* label)
        {
            return option.rfind(label, 0) == 0;
        };

        bool handled = false;
        bool changed = false;

        if (menu_selected == &menu_video)
        {
            if (selected(ENTRY_FULLSCREEN))
            {
                handled = true;
                const int next = wrap_value(
                    config.video.mode,
                    video_settings_t::MODE_WINDOW,
                    video_settings_t::MODE_STRETCH,
                    1,
                    direction);
                if (next != config.video.mode)
                {
                    config.video.mode = next;
                    config.videoRestartRequired = true;
                    changed = true;
                }
            }
            else if (selected(ENTRY_SCALE))
            {
                handled = true;
                const int next = wrap_value(config.video.scale, 1, 3, 1, direction);
                if (next != config.video.scale)
                {
                    config.video.scale = next;
                    config.videoRestartRequired = true;
                    changed = true;
                }
            }
            else if (selected(ENTRY_WIDESCREEN))
            {
                handled = true;
                const int next = wrap_value(config.video.widescreen, 0, 2, 1, direction);
                if (next != config.video.widescreen)
                {
                    config.video.widescreen = next;
                    config.videoRestartRequired = true;
                    changed = true;
                }
            }
            else if (option.rfind("PIXEL SCALER ", 0) == 0)
            {
                handled = true;
                const int current = pixel_scaler::normalize(
                    pixel_scaler::mode.load(std::memory_order_relaxed));
                int next = current;

                if (direction > 0)
                {
                    next = pixel_scaler::cycle();
                }
                else
                {
                    switch (current)
                    {
                        case pixel_scaler::OFF:      next = pixel_scaler::HQX_4X;  break;
                        case pixel_scaler::XBRZ_3X:  next = pixel_scaler::OFF;     break;
                        case pixel_scaler::XBRZ_4X:  next = pixel_scaler::XBRZ_3X; break;
                        case pixel_scaler::XBRZ_5X:  next = pixel_scaler::XBRZ_4X; break;
                        case pixel_scaler::XBRZ_6X:  next = pixel_scaler::XBRZ_5X; break;
                        case pixel_scaler::HQX_3X:   next = pixel_scaler::XBRZ_6X; break;
                        case pixel_scaler::HQX_4X:   next = pixel_scaler::HQX_3X;  break;
                        default:                     next = pixel_scaler::OFF;     break;
                    }
                    pixel_scaler::set(next);
                    pixel_scaler::request_transition_restart(current, next);
                }

                menu_video[cursor] =
                    std::string("PIXEL SCALER ") + pixel_scaler::name(next);
                changed = next != current;
            }
            else if (selected(ENTRY_FPS_COUNTER))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.video.fps_count != target)
                {
                    config.video.fps_count = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_X_OFFSET))
            {
                handled = true;
                const int next = wrap_value(config.video.x_offset, -100, 100, 5, direction);
                if (next != config.video.x_offset)
                {
                    config.video.x_offset = next;
                    changed = true;
                }
            }
            else if (selected(ENTRY_Y_OFFSET))
            {
                handled = true;
                const int next = wrap_value(config.video.y_offset, -100, 100, 5, direction);
                if (next != config.video.y_offset)
                {
                    config.video.y_offset = next;
                    changed = true;
                }
            }
        }
        else if (menu_selected == &menu_crt_shader1)
        {
            if (selected(ENTRY_CRT_SHADER_MODE))
            {
                handled = true;
                const int next = wrap_value(
                    config.video.shader_mode,
                    video_settings_t::SHADER_OFF,
                    video_settings_t::SHADER_FULL,
                    1,
                    direction);

                if (next != config.video.shader_mode)
                {
                    config.video.shader_mode = next;
                    if (next == video_settings_t::SHADER_OFF)
                        display_message("MOST EFFECTS WILL BE UNAVAILABLE");
                    else if (next == video_settings_t::SHADER_FAST)
                        display_message("NOISE AND DESAT. WILL BE UNAVAILBLE");
                    else
                        display_message("ALL EFFECTS ENABLED");

                    config.videoRestartRequired = true;
                    changed = true;
                }
            }
        }
        else if (menu_selected == &menu_crt_mask_settings)
        {
            if (selected(ENTRY_SHADOW_MASK))
            {
                handled = true;
                if (config.video.shader_mode == video_settings_t::SHADER_OFF)
                    display_message("ENABLE SHADER FIRST");
                else
                {
                    const int target = direction > 0
                        ? video_settings_t::SHADOW_MASK_SHADER
                        : video_settings_t::SHADOW_MASK_OFF;
                    if (config.video.shadow_mask != target)
                    {
                        config.video.shadow_mask = target;
                        changed = true;
                    }
                }
            }
            else if (selected(ENTRY_MASK_DIM))
            {
                handled = true;
                if (config.video.shadow_mask == video_settings_t::SHADOW_MASK_OFF)
                    display_message("ENABLE MASK FIRST");
                else if (config.video.shader_mode == video_settings_t::SHADER_OFF)
                    display_message("ENABLE SHADER FIRST");
                else
                {
                    config.video.maskDim = wrap_value(config.video.maskDim, 0, 100, 5, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_MASK_BOOST))
            {
                handled = true;
                if (config.video.shadow_mask == video_settings_t::SHADOW_MASK_OFF)
                    display_message("ENABLE MASK FIRST");
                else if (config.video.shader_mode == video_settings_t::SHADER_OFF)
                    display_message("ENABLE SHADER FIRST");
                else
                {
                    config.video.maskBoost = wrap_value(config.video.maskBoost, 100, 160, 5, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_MASK_SIZE))
            {
                handled = true;
                if (config.video.shadow_mask == video_settings_t::SHADOW_MASK_OFF)
                    display_message("ENABLE MASK FIRST");
                else if (config.video.shader_mode == video_settings_t::SHADER_OFF)
                    display_message("ENABLE SHADER FIRST");
                else
                {
                    config.video.mask_size = wrap_value(config.video.mask_size, 3, 6, 1, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_SCANLINES))
            {
                handled = true;
                if (config.video.shader_mode == video_settings_t::SHADER_OFF)
                    display_message("ENABLE SHADER FIRST");
                else
                {
                    config.video.scanlines = wrap_value(config.video.scanlines, 0, 3, 1, direction);
                    changed = true;
                }
            }
        }
        else if (menu_selected == &menu_crt_shape_settings)
        {
            if (selected(ENTRY_CRT_SHAPE))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.video.crt_shape != target)
                {
                    config.video.crt_shape = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_VIGNETTE))
            {
                handled = true;
                config.video.vignette = wrap_value(config.video.vignette, 0, 75, 5, direction);
                changed = true;
            }
            else if (selected(ENTRY_WARPX))
            {
                handled = true;
                if (config.video.shader_mode == video_settings_t::SHADER_OFF)
                    display_message("ENABLE SHADER FIRST");
                else
                {
                    config.video.warpX = wrap_value(config.video.warpX, 0, 10, 1, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_WARPY))
            {
                handled = true;
                if (config.video.shader_mode == video_settings_t::SHADER_OFF)
                    display_message("ENABLE SHADER FIRST");
                else
                {
                    config.video.warpY = wrap_value(config.video.warpY, 0, 10, 1, direction);
                    changed = true;
                }
            }
        }
        else if (menu_selected == &menu_crt_shader2)
        {
            if (selected(ENTRY_NOISE))
            {
                handled = true;
                if (config.video.shader_mode != video_settings_t::SHADER_FULL)
                    display_message("ENABLE FULL SHADER FIRST");
                else
                {
                    config.video.noise = wrap_value(config.video.noise, 0, 20, 1, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_DESATURATE))
            {
                handled = true;
                if (config.video.shader_mode != video_settings_t::SHADER_FULL)
                    display_message("ENABLE FULL SHADER FIRST");
                else
                {
                    config.video.desaturate = wrap_value(config.video.desaturate, 0, 10, 1, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_DESATURATE_EDGES))
            {
                handled = true;
                if (config.video.shader_mode != video_settings_t::SHADER_FULL)
                    display_message("ENABLE FULL SHADER FIRST");
                else
                {
                    config.video.desaturate_edges = wrap_value(config.video.desaturate_edges, 0, 10, 1, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_BRIGHTNESS_BOOST))
            {
                handled = true;
                if (config.video.shader_mode == video_settings_t::SHADER_OFF)
                    display_message("ENABLE SHADER FIRST");
                else
                {
                    config.video.brightboost = wrap_value(config.video.brightboost, 0, 10, 1, direction);
                    changed = true;
                }
            }
        }
        else if (menu_selected == &menu_blargg_filter)
        {
            if (selected(ENTRY_BLARGG))
            {
                handled = true;
                const int previous = config.video.blargg;
                const int next = wrap_value(
                    previous,
                    video_settings_t::BLARGG_DISABLE,
                    video_settings_t::BLARGG_RGB,
                    1,
                    direction);
                if (next != previous)
                {
                    config.video.blargg = next;
                    if (previous == video_settings_t::BLARGG_DISABLE ||
                        next == video_settings_t::BLARGG_DISABLE)
                        config.videoRestartRequired = true;
                    changed = true;
                }
            }
            else if (selected(ENTRY_SATURATION))
            {
                handled = true;
                if (config.video.blargg == video_settings_t::BLARGG_DISABLE)
                    display_message("ENABLE BLARGG FILTER FIRST");
                else
                {
                    config.video.saturation = wrap_value(config.video.saturation, -50, 50, 10, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_CONTRAST))
            {
                handled = true;
                if (config.video.blargg == video_settings_t::BLARGG_DISABLE)
                    display_message("ENABLE BLARGG FILTER FIRST");
                else
                {
                    config.video.contrast = wrap_value(config.video.contrast, -50, 50, 10, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_BRIGHTNESS))
            {
                handled = true;
                if (config.video.blargg == video_settings_t::BLARGG_DISABLE)
                    display_message("ENABLE BLARGG FILTER FIRST");
                else
                {
                    config.video.brightness = wrap_value(config.video.brightness, -50, 50, 10, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_SHARPNESS))
            {
                handled = true;
                if (config.video.blargg == video_settings_t::BLARGG_DISABLE)
                    display_message("ENABLE BLARGG FILTER FIRST");
                else
                {
                    config.video.sharpness = wrap_value(config.video.sharpness, -50, 50, 10, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_RESOLUTION))
            {
                handled = true;
                if (config.video.blargg == video_settings_t::BLARGG_DISABLE)
                    display_message("ENABLE BLARGG FILTER FIRST");
                else
                {
                    config.video.resolution = wrap_value(config.video.resolution, -100, 0, 10, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_GAMMA))
            {
                handled = true;
                if (config.video.blargg == video_settings_t::BLARGG_DISABLE)
                    display_message("ENABLE BLARGG FILTER FIRST");
                else
                {
                    config.video.gamma = wrap_value(config.video.gamma, -20, 10, 1, direction);
                    changed = true;
                }
            }
            else if (selected(ENTRY_HUE))
            {
                handled = true;
                if (config.video.blargg == video_settings_t::BLARGG_DISABLE)
                    display_message("ENABLE BLARGG FILTER FIRST");
                else
                {
                    config.video.hue = wrap_value(config.video.hue, -10, 10, 1, direction);
                    changed = true;
                }
            }
        }
        else if (menu_selected == &menu_sound)
        {
            if (selected(ENTRY_MUTE))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.sound.enabled != target)
                {
                    config.sound.enabled = target;
                    if (config.sound.enabled)
                        cannonball::audio.start_audio();
                    else
                        cannonball::audio.stop_audio();
                    changed = true;
                }
            }
            else if (selected(ENTRY_ADVERTISE))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.sound.advertise != target)
                {
                    config.sound.advertise = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_PREVIEWSND))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.sound.preview != target)
                {
                    config.sound.preview = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_FIXSAMPLES))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.sound.fix_samples != target)
                {
                    if (roms.load_pcm_rom(target == 1) == 0)
                    {
                        config.sound.fix_samples = target;
                        display_message(target == 1 ? "FIXED SAMPLES LOADED" : "ORIGINAL SAMPLES LOADED");
                        changed = true;
                    }
                    else
                    {
                        display_message(target == 1 ? "CANT LOAD FIXED SAMPLES" : "CANT LOAD ORIGINAL SAMPLES");
                    }
                }
            }
        }
        else if (menu_selected == &menu_controls)
        {
            if (selected(ENTRY_GEAR))
            {
                handled = true;
                config.controls.gear = wrap_value(
                    config.controls.gear,
                    config.controls.GEAR_BUTTON,
                    config.controls.GEAR_AUTO,
                    1,
                    direction);
                changed = true;
            }
            else if (selected(ENTRY_INVERT_ACCEL))
            {
                handled = true;
                const bool target = direction > 0;
                if (config.controls.invert[1] != target)
                {
                    config.controls.invert[1] = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_INVERT_BRAKE))
            {
                handled = true;
                const bool target = direction > 0;
                if (config.controls.invert[2] != target)
                {
                    config.controls.invert[2] = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_FFB))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;

                if (target != config.controls.haptic)
                {
                    if (target)
                    {
                        if (!forcefeedback::is_supported())
                        {
                            config.controls.haptic =
                                forcefeedback::init(
                                    config.controls.max_force,
                                    config.controls.min_force,
                                    config.controls.force_duration)
                                ? 1 : 0;
                        }
                        else
                        {
                            config.controls.haptic = 1;
                        }

                        if (config.controls.haptic)
                        {
                            forcefeedback::set_gain(config.controls.ffb_strength);
                            forcefeedback::set_enabled(true);
                            changed = true;
                        }
                        else
                        {
                            display_message("NO FORCE FEEDBACK DEVICE FOUND");
                        }
                    }
                    else
                    {
                        config.controls.haptic = 0;
                        forcefeedback::set_enabled(false);
                        changed = true;
                    }
                }
            }
            else if (selected(ENTRY_FFB_STRENGTH))
            {
                handled = true;
                config.controls.ffb_strength = wrap_value(
                    config.controls.ffb_strength, 10, 100, 10, direction);
                forcefeedback::set_gain(config.controls.ffb_strength);
                changed = true;
            }
            else if (selected(ENTRY_CENTERING_STRENGTH))
            {
                handled = true;
                config.controls.centering_strength = wrap_value(
                    config.controls.centering_strength, 0, 100, 10, direction);
                forcefeedback::set_centering_strength(
                    (config.controls.centering_strength * 40 + 50) / 100);
                changed = true;
            }
            else if (option.rfind("GAMEPAD RUMBLE ", 0) == 0)
            {
                handled = true;
                const bool target = direction > 0;
                if (gamepad_rumble::enabled != target)
                {
                    gamepad_rumble::enabled = target;
                    input.set_rumble(false, config.controls.rumble, 0);
                    menu_controls[cursor] =
                        std::string("GAMEPAD RUMBLE ") + (target ? "ON" : "OFF");
                    changed = true;
                }
            }
            else if (selected(ENTRY_RUMBLE))
            {
                handled = true;
                int level = static_cast<int>(config.controls.rumble * 4.0f + 0.5f);
                if (level < 1 || level > 4)
                    level = 1;
                level = wrap_value(level, 1, 4, 1, direction);
                config.controls.rumble = level * 0.25f;
                changed = true;
            }
            else if (selected(ENTRY_DSTEER))
            {
                handled = true;
                config.controls.steer_speed = wrap_value(
                    config.controls.steer_speed, 1, 9, 1, direction);
                changed = true;
            }
            else if (selected(ENTRY_DPEDAL))
            {
                handled = true;
                config.controls.pedal_speed = wrap_value(
                    config.controls.pedal_speed, 1, 9, 1, direction);
                changed = true;
            }
        }
        else if (menu_selected == &menu_engine)
        {
            if (selected(ENTRY_TIME))
            {
                handled = true;
                int state_index = config.engine.freeze_timer ? 4 : config.engine.dip_time;
                state_index = wrap_value(state_index, 0, 4, 1, direction);
                config.engine.freeze_timer = state_index == 4;
                config.engine.dip_time = state_index == 4 ? 3 : state_index;
                changed = true;
            }
            else if (selected(ENTRY_TRAFFIC))
            {
                handled = true;
                int state_index = config.engine.disable_traffic ? 4 : config.engine.dip_traffic;
                state_index = wrap_value(state_index, 0, 4, 1, direction);
                config.engine.disable_traffic = state_index == 4;
                config.engine.dip_traffic = state_index == 4 ? 3 : state_index;
                changed = true;
            }
            else if (selected(ENTRY_FREEPLAY))
            {
                handled = true;
                const bool target = direction > 0;
                if (config.engine.freeplay != target)
                {
                    config.engine.freeplay = target;
                    changed = true;
                }
            }
            else if (option.rfind("SELECTION TIMER ", 0) == 0)
            {
                handled = true;
                const int current = config.selection_timer_seconds();
                int next = current;

                if (direction > 0)
                    next = current == 0 ? 15 : (current == 15 ? 30 : 0);
                else
                    next = current == 0 ? 30 : (current == 30 ? 15 : 0);

                config.set_selection_timer_seconds(next);
                menu_engine[cursor] =
                    std::string("SELECTION TIMER ") +
                    (next == 0 ? "OFF" : std::to_string(next) + " SEC");
                changed = next != current;
            }
        }
        else if (menu_selected == &menu_enhancements)
        {
            if (selected(ENTRY_HIRES))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.video.hires_next != target || config.video.hires != target)
                {
                    config.video.hires_next = target;
                    if (target == 0)
                        config.video.hiresprites = 0;
                    config.videoRestartRequired = true;
                    changed = true;
                }
            }
            else if (selected(ENTRY_SPRITERES))
            {
                handled = true;
                if (config.video.hires == 0)
                {
                    display_message("Set game engine to hires first");
                }
                else
                {
                    const int target = direction > 0 ? 1 : 0;
                    if (config.video.hiresprites != target)
                    {
                        config.video.hiresprites = target;
                        display_message(target ? "Enabled. Toggle in game with F7." : "Disabled. Toggle in game with F7.");
                        changed = true;
                    }
                }
            }
            else if (selected(ENTRY_ATTRACT))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.engine.new_attract != target)
                {
                    config.engine.new_attract = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_OBJECTS))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.engine.level_objects != target)
                {
                    config.engine.level_objects = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_PROTOTYPE))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.engine.prototype != target)
                {
                    config.engine.prototype = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_TIMER))
            {
                handled = true;
                const bool target = direction > 0;
                if (config.engine.fix_timer != target)
                {
                    config.engine.fix_timer = target;
                    changed = true;
                }
            }
        }
        else if (menu_selected == &menu_handling)
        {
            if (selected(ENTRY_GRIP))
            {
                handled = true;
                const bool target = direction > 0;
                if (config.engine.grippy_tyres != target)
                {
                    config.engine.grippy_tyres = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_OFFROAD))
            {
                handled = true;
                const bool target = direction > 0;
                if (config.engine.offroad != target)
                {
                    config.engine.offroad = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_BUMPER))
            {
                handled = true;
                const bool target = direction > 0;
                if (config.engine.bumper != target)
                {
                    config.engine.bumper = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_TURBO))
            {
                handled = true;
                const bool target = direction > 0;
                if (config.engine.turbo != target)
                {
                    config.engine.turbo = target;
                    changed = true;
                }
            }
            else if (selected(ENTRY_COLOR))
            {
                handled = true;
                config.engine.car_pal = wrap_value(config.engine.car_pal, 0, 7, 1, direction);
                changed = true;
            }
        }
        else if (menu_selected == &menu_musictest)
        {
            if (selected(ENTRY_MUSIC2))
            {
                handled = true;
                music_track = wrap_value(
                    music_track,
                    0,
                    static_cast<int>(config.sound.music.size()),
                    1,
                    direction);
                changed = true;
            }
            else if (selected(ENTRY_WAVEVOLUME))
            {
                handled = true;
                config.sound.wave_volume = wrap_value(config.sound.wave_volume, 1, 8, 1, direction);
                changed = true;
            }
            else if (selected(ENTRY_CALLBACK_RATE))
            {
                handled = true;
                const int target = direction > 0 ? 1 : 0;
                if (config.sound.callback_rate != target)
                {
                    config.sound.callback_rate = target;
                    cannonball::audio.stop_audio();
                    cannonball::audio.init();
                    changed = true;
                }
            }
        }
        else if (menu_selected == &menu_system)
        {
            if (selected(ENTRY_MASTER_BREAK))
            {
                handled = true;
                const int target = direction > 0 ? SDLK_F10 : SDLK_ESCAPE;
                if (config.master_break_key != target)
                {
                    config.master_break_key = target;
                    menu_system[cursor] =
                        std::string(ENTRY_MASTER_BREAK) +
                        (target == SDLK_ESCAPE ? "ESC" : "F10");
                    changed = true;
                }
            }
        }

        if (changed)
            directional_save_pending = true;

        return handled;
    }

    // Route only the new shallow DX category pages here. Every ordinary option
    // is still handled by the preserved SE implementation below this wrapper.
    void tick_menu() override
    {
        if (directional_save_pending && !config.videoRestartRequired)
        {
            directional_save_pending = false;
            if (!config.save())
                display_message("ERROR SAVING SETTINGS!");
        }

        // Keyboard arrows and controller D-Pads share the logical LEFT/RIGHT
        // actions, so a single path gives both devices symmetric value editing.
        if (!config.smartypi.enabled)
        {
            int direction = 0;
            if (input.has_pressed(Input::LEFT))
                direction = -1;
            else if (input.has_pressed(Input::RIGHT))
                direction = 1;

            if (direction != 0 && adjust_current_value(direction))
            {
                osoundint.queue_sound(sound::BEEP1);
                refresh_menu();
                return;
            }
        }

        if (!config.smartypi.enabled && menu_selected == &menu_settings)
        {
            if (!select_pressed())
            {
                MenuBase::tick_menu();
                return;
            }

            const std::string& option = menu_settings[cursor];

            if (option == "CONTROLS")
            {
                populate_controls();

                // DX bindings already encode whether steering/pedals use an
                // axis or a digital control, so the old global ANALOG mode is
                // no longer a meaningful user setting.
                for (auto it = menu_controls.begin(); it != menu_controls.end(); )
                {
                    if (it->rfind(ENTRY_ANALOG, 0) == 0)
                        it = menu_controls.erase(it);
                    else
                        ++it;
                }

                // Put the detailed per-effect tuning directly beside the
                // existing FFB master and Spring controls.
                const auto spring_entry = std::find_if(
                    menu_controls.begin(),
                    menu_controls.end(),
                    [](const std::string& entry)
                    {
                        return entry.rfind(ENTRY_CENTERING_STRENGTH, 0) == 0;
                    });

                if (spring_entry != menu_controls.end())
                    menu_controls.insert(spring_entry + 1, "FFB TUNING");
                else
                    menu_controls.insert(menu_controls.end() - 1, "FFB TUNING");

                set_menu(&menu_controls);
            }
            else if (option == "VIDEO")
            {
                set_menu(&menu_video);
            }
            else if (option == "AUDIO")
            {
                set_menu(&menu_sound);
            }
            else if (option == "GAMEPLAY")
            {
                set_menu(&menu_engine);
            }
            else if (option == "ENHANCEMENTS")
            {
                set_menu(&menu_enhancements);
            }
            else if (option == "SYSTEM")
            {
                menu_system.clear();
                menu_system.push_back(
                    std::string(ENTRY_MASTER_BREAK) +
                    (config.master_break_key == SDLK_ESCAPE ? "ESC" : "F10"));
                menu_system.push_back(ENTRY_SCORES);
                menu_system.push_back(ENTRY_BACK);
                set_menu(&menu_system);
            }
            else if (option.rfind(ENTRY_BACK, 0) == 0)
            {
                menu_back();
            }

            refresh_menu();
            return;
        }

        if (!config.smartypi.enabled &&
            (menu_selected == &menu_ffb_tuning ||
             menu_selected == &menu_ffb_effects ||
             menu_selected == &menu_ffb_spring))
        {
            tick_ffb_tuning_menu();
            return;
        }

        if (!config.smartypi.enabled &&
            menu_selected == &menu_controls &&
            cursor >= 0 &&
            cursor < static_cast<int>(menu_controls.size()) &&
            menu_controls[cursor] == "FFB TUNING")
        {
            if (!select_pressed())
            {
                MenuBase::tick_menu();
                return;
            }

            populate_ffb_tuning();
            set_menu(&menu_ffb_tuning);
            refresh_menu();
            return;
        }

        if (!config.smartypi.enabled && menu_selected == &menu_system)
        {
            if (!select_pressed())
            {
                MenuBase::tick_menu();
                return;
            }

            const std::string& option = menu_system[cursor];

            if (option.rfind(ENTRY_MASTER_BREAK, 0) == 0)
            {
                config.master_break_key =
                    config.master_break_key == SDLK_ESCAPE ? SDLK_F10 : SDLK_ESCAPE;
                menu_system[cursor] =
                    std::string(ENTRY_MASTER_BREAK) +
                    (config.master_break_key == SDLK_ESCAPE ? "ESC" : "F10");
            }
            else if (option.rfind(ENTRY_SCORES, 0) == 0)
            {
                display_message(
                    config.clear_scores()
                        ? "SCORES CLEARED"
                        : "NO SAVED SCORES FOUND!");
            }
            else if (option.rfind(ENTRY_BACK, 0) == 0)
            {
                menu_back();
            }

            refresh_menu();
            return;
        }

        // Pixel Scaler now belongs to VIDEO. Intercept only this one entry;
        // all other video settings continue through the original handler.
        if (!config.smartypi.enabled &&
            menu_selected == &menu_video &&
            cursor >= 0 &&
            cursor < static_cast<int>(menu_video.size()) &&
            menu_video[cursor].rfind("PIXEL SCALER ", 0) == 0)
        {
            if (!select_pressed())
            {
                MenuBase::tick_menu();
                return;
            }

            pixel_scaler::cycle();
            menu_video[cursor] =
                std::string("PIXEL SCALER ") +
                pixel_scaler::name(
                    pixel_scaler::mode.load(std::memory_order_relaxed));
            return;
        }

        MenuBase::tick_menu();
    }

    void populate_controls() override;
    bool select_pressed() override;
    void redefine_joystick() override;
};
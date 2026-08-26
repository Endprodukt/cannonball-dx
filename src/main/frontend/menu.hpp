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
#include "frontend/ttrial.hpp"
#include "sdl2/input.hpp"
#include "sdl2/pixel_scaler_state.hpp"
#include "engine/audio/osoundint.hpp"

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

    // Route only the new shallow DX category pages here. Every ordinary option
    // is still handled by the preserved SE implementation below this wrapper.
    void tick_menu() override
    {
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
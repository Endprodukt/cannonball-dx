/***************************************************************************
    Front End Menu System.

    This file is part of Cannonball.
    Copyright (c) Chris White.
    See license.txt for more details.

    Modifications for CannonBall-SE Copyright (c) 2025, James Pearce
***************************************************************************/

#pragma once

#include <vector>
#include <string>
#include "stdint.hpp"
#include "main.hpp"
#include "frontend/ttrial.hpp"
#include "sdl2/pixel_scaler_state.hpp"

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
    // DX-only submenu for infrequently used administrative options.
    std::vector<std::string> menu_system;

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
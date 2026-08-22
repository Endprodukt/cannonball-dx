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

    // Number of seconds to show messages for
    const static int32_t MESSAGE_TIME = 5;

    // Message text
    std::string msg;

    // Cursor
    int16_t cursor;

    struct menu_pair
    {
        int cursor;
        std::vector<std::string>* menu;
    };

    std::vector<menu_pair> menu_stack;

    // Menu Selected
    std::vector<std::string>* menu_selected;

    // Is a text menu (i.e. no mini-car shown)
    bool is_text_menu;

    // Menu Entries
    std::vector<std::string> menu_main;
    std::vector<std::string> menu_gamemodes;
    std::vector<std::string> menu_cont;
    std::vector<std::string> menu_timetrial;
    std::vector<std::string> menu_about;
    std::vector<std::string> menu_settings;
    std::vector<std::string> menu_video;
    std::vector<std::string> menu_sound;
    std::vector<std::string> menu_controls;
    std::vector<std::string> menu_controls_gp;
    std::vector<std::string> menu_engine;
    std::vector<std::string> menu_enhancements;
    std::vector<std::string> menu_handling;
    std::vector<std::string> menu_musictest;

    // Cabinet-specific Menus
    std::vector<std::string> menu_s_dips;
    std::vector<std::string> menu_s_exsettings;
    std::vector<std::string> menu_s_tests;
    std::vector<std::string> menu_s_enhance;

    // CRT shader menus
    std::vector<std::string> menu_crt_shader1;
    std::vector<std::string> menu_crt_shape_settings;
    std::vector<std::string> menu_crt_mask_settings;
    std::vector<std::string> menu_crt_shader2;
    std::vector<std::string> menu_blargg_filter;

    // Text strings used during controls redefinition
    std::vector<std::string> text_redefine;

    // Main Menu Functions
    void populate_for_pc();
    void populate_for_cabinet();
    virtual void populate_controls();

    void tick_ui();
    void draw_menu_options();
    void draw_text(std::string s);
    void tick_menu();
    virtual bool select_pressed();

    void set_menu(std::vector<std::string>* menu);
    void menu_back();
    void refresh_menu();
    void set_menu_text(std::string s1, std::string s2);

    void redefine_keyboard();
    virtual void redefine_joystick();

    void display_message(std::string s);
    bool check_jap_roms();
    void start_game(int mode, int settings = 0);
};

class Menu : public MenuBase
{
public:
    Menu() = default;
    ~Menu() override = default;

    // Populate the existing menus, then remove the now-obsolete Continuous
    // traffic selector. Continuous traffic follows the normal OutRun DIP
    // difficulty automatically; the legacy config value remains loadable for
    // backwards compatibility but is no longer presented to the player.
    void populate()
    {
        MenuBase::populate();

        if (!menu_about.empty())
            menu_about[0] = std::string("CANNONBALL DX ") + CANNONBALL_DX_VERSION;

        // Keep the inherited CannonBall-SE credit, but identify the DX fork
        // and current maintainer directly above it.
        if (menu_about.size() >= 6)
            menu_about.insert(menu_about.begin() + 5, "DX BUILD COPYRIGHT 2026 ENDPRODUKT");

        for (auto it = menu_cont.begin(); it != menu_cont.end(); )
        {
            if (it->rfind("TRAFFIC ", 0) == 0)
                it = menu_cont.erase(it);
            else
                ++it;
        }

        const int scaler_mode =
            pixel_scaler::mode.load(std::memory_order_relaxed);
        const std::string scaler_entry =
            std::string("PIXEL SCALER ") + pixel_scaler::name(scaler_mode);

        if (!menu_enhancements.empty())
            menu_enhancements.insert(menu_enhancements.end() - 1, scaler_entry);
        else
            menu_enhancements.push_back(scaler_entry);
    }

    // Wrapper hook used to keep analog steering from moving normal menu
    // cursors while leaving in-game steering untouched.
    void tick();

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
    void populate_controls() override;
    bool select_pressed() override;
    void redefine_joystick() override;
};

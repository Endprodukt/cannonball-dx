/***************************************************************************
    Front End Menu System - CannonBall DX Endless settings extension.

    Keep the current DX menu implementation intact as MenuLegacy and layer the
    Endless configuration page on top. This avoids duplicating the large DX
    frontend while still giving Endless its own persistent tuning page.
***************************************************************************/

#pragma once

// Pre-include the legacy header dependencies before the temporary class-name
// macro below, so it can only rename the menu class itself.
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

#define Menu MenuLegacy
#include "menu_legacy.hpp.inc"
#undef Menu

// MenuLegacy is never used directly, but its construction vtable still needs
// concrete implementations for the three virtual hooks that the normal
// menu.cpp provides for the final Menu class below.
inline void MenuLegacy::populate_controls()
{
    MenuBase::populate_controls();
}

inline bool MenuLegacy::select_pressed()
{
    return MenuBase::select_pressed();
}

inline void MenuLegacy::redefine_joystick()
{
    MenuBase::redefine_joystick();
}

class Menu : public MenuLegacy
{
public:
    Menu() = default;
    ~Menu() override = default;

    void populate()
    {
        MenuLegacy::populate();

        if (config.smartypi.enabled)
            return;

        // Gameplay is the natural home for rules that affect an Endless run.
        // Keep the main settings page compact and expose the detailed values
        // only after the player explicitly opens ENDLESS SETTINGS.
        auto existing = std::find(
            menu_engine.begin(), menu_engine.end(), "ENDLESS SETTINGS");
        if (existing == menu_engine.end())
        {
            auto insert_before = std::find_if(
                menu_engine.begin(),
                menu_engine.end(),
                [](const std::string& entry)
                {
                    return entry.rfind(ENTRY_SUB_HANDLING, 0) == 0;
                });
            menu_engine.insert(insert_before, "ENDLESS SETTINGS");
        }
    }

    // Existing out-of-line DX implementations in menu.cpp remain the final
    // hooks used by the application.
    void tick();
    void handle_escape();

protected:
    std::vector<std::string> menu_endless;

    static int adjust_time_value(int value, int direction, int minimum)
    {
        if (direction > 0)
        {
            if (value >= 95)
                return 99;
            value += 5;
            if (value > 99)
                value = 99;
        }
        else
        {
            if (value == 99)
                return 95;
            value -= 5;
            if (value < minimum)
                value = minimum;
        }
        return value;
    }

    static std::string sec_text(const char* label, int value)
    {
        return std::string(label) + " " + std::to_string(value) + " SEC";
    }

    static std::string value_text(const char* label, int value)
    {
        return std::string(label) + " " + std::to_string(value);
    }

    static std::string stages_text(const char* label, int value)
    {
        return std::string(label) + " " + std::to_string(value) + " STAGES";
    }

    void populate_endless_settings()
    {
        menu_endless.clear();
        menu_endless.push_back(sec_text("START TIME", config.endless_start_time()));
        menu_endless.push_back(sec_text("CHECKPOINT TIME", config.endless_checkpoint_time()));
        menu_endless.push_back(sec_text("TIME DECREASE", config.endless_time_decrease()));
        menu_endless.push_back(stages_text("DECREASE EVERY", config.endless_time_interval()));
        menu_endless.push_back(sec_text("MIN CHECKPOINT", config.endless_min_checkpoint()));
        menu_endless.push_back(value_text("START TRAFFIC", config.endless_start_traffic()));
        menu_endless.push_back(value_text("TRAFFIC INCREASE", config.endless_traffic_increase()));
        menu_endless.push_back(stages_text("INCREASE EVERY", config.endless_traffic_interval()));
        menu_endless.push_back(value_text("MAX TRAFFIC", config.endless_max_traffic()));
        menu_endless.push_back("RESET TO DEFAULTS");
        menu_endless.push_back(ENTRY_BACK);
    }

    bool adjust_endless_value(int row, int direction)
    {
        if (direction == 0)
            return false;

        switch (row)
        {
            case 0:
                config.set_endless_start_time(
                    adjust_time_value(config.endless_start_time(), direction, 30));
                break;

            case 1:
                config.set_endless_checkpoint_time(
                    adjust_time_value(config.endless_checkpoint_time(), direction, 20));
                break;

            case 2:
            {
                int value = config.endless_time_decrease() + direction;
                if (value < 0) value = 0;
                if (value > 10) value = 10;
                config.set_endless_time_decrease(value);
                break;
            }

            case 3:
            {
                int value = config.endless_time_interval() + direction;
                if (value < 1) value = 1;
                if (value > 10) value = 10;
                config.set_endless_time_interval(value);
                break;
            }

            case 4:
                config.set_endless_min_checkpoint(
                    adjust_time_value(config.endless_min_checkpoint(), direction, 10));
                break;

            case 5:
            {
                int value = config.endless_start_traffic() + direction;
                if (value < 0) value = 0;
                if (value > 8) value = 8;
                config.set_endless_start_traffic(value);
                break;
            }

            case 6:
            {
                int value = config.endless_traffic_increase() + direction;
                if (value < 0) value = 0;
                if (value > 8) value = 8;
                config.set_endless_traffic_increase(value);
                break;
            }

            case 7:
            {
                int value = config.endless_traffic_interval() + direction;
                if (value < 1) value = 1;
                if (value > 10) value = 10;
                config.set_endless_traffic_interval(value);
                break;
            }

            case 8:
            {
                int value = config.endless_max_traffic() + direction;
                if (value < 0) value = 0;
                if (value > 8) value = 8;
                config.set_endless_max_traffic(value);
                break;
            }

            default:
                return false;
        }

        const int old_cursor = cursor;
        populate_endless_settings();
        cursor = old_cursor;
        return true;
    }

    void tick_menu() override
    {
        // Open the Endless page from Gameplay. All other Gameplay entries are
        // still owned by the existing DX menu implementation.
        if (!config.smartypi.enabled &&
            menu_selected == &menu_engine &&
            cursor >= 0 &&
            cursor < static_cast<int>(menu_engine.size()) &&
            menu_engine[cursor] == "ENDLESS SETTINGS")
        {
            if (!select_pressed())
            {
                MenuLegacy::tick_menu();
                return;
            }

            populate_endless_settings();
            set_menu(&menu_endless);
            refresh_menu();
            return;
        }

        if (!config.smartypi.enabled && menu_selected == &menu_endless)
        {
            int direction = 0;
            if (input.has_pressed(Input::RIGHT))
                direction = 1;
            else if (input.has_pressed(Input::LEFT))
                direction = -1;

            if (direction && adjust_endless_value(cursor, direction))
            {
                config.save();
                osoundint.queue_sound(sound::BEEP1);
                return;
            }

            if (!select_pressed())
            {
                MenuBase::tick_menu();
                return;
            }

            if (cursor >= 0 && cursor <= 8)
            {
                if (adjust_endless_value(cursor, 1))
                {
                    config.save();
                    osoundint.queue_sound(sound::BEEP1);
                }
                return;
            }

            if (cursor == 9)
            {
                config.reset_endless_settings();
                config.save();
                populate_endless_settings();
                cursor = 9;
                osoundint.queue_sound(sound::BEEP1);
                return;
            }

            if (cursor == 10)
            {
                menu_back();
                refresh_menu();
                osoundint.queue_sound(sound::BEEP1);
                return;
            }
        }

        MenuLegacy::tick_menu();
    }

    void populate_controls() override;
    bool select_pressed() override;
    void redefine_joystick() override;
};

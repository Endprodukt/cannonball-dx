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

        // DX Time Trial records are directly comparable because every run is
        // exactly three laps. Keep that rule fixed in the frontend and remove
        // the old variable-lap option so the menu cannot create incompatible
        // five-lap (or shorter) runs anymore.
        config.ttrial.laps = 3;
        menu_timetrial.erase(
            std::remove(menu_timetrial.begin(), menu_timetrial.end(), ENTRY_LAPS),
            menu_timetrial.end());

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

    static const FfbMenuItem* dx_ffb_effect_items(int& count)
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
            { "ENGINE VIBRATION",        "start_rev_shake" },
            { "ENGINE SPEED",            "engine_speed" },
        };

        count = static_cast<int>(sizeof(ITEMS) / sizeof(ITEMS[0]));
        return ITEMS;
    }

    void populate_ffb_effects_dx()
    {
        int item_count = 0;
        const FfbMenuItem* items = dx_ffb_effect_items(item_count);
        const int page_size = 9;
        const int first = ffb_effect_page * page_size;
        const int last = std::min(first + page_size, item_count);

        menu_ffb_effects.clear();
        for (int i = first; i < last; ++i)
        {
            int value = 0;
            if (std::string(items[i].setting) == "engine_speed")
            {
                // Stored 0..100 maps to a user-facing 50..150%. This lets us
                // reuse the existing persistent per-effect XML path while 100%
                // remains the neutral/current motor-sine speed.
                value = 50 + config.ffb_effect_setting("engine_speed", 50);
            }
            else
            {
                value = config.ffb_effect_setting(items[i].setting, 0);
            }

            menu_ffb_effects.push_back(
                ffb_value_text(items[i].label, value));
        }

        if (ffb_effect_page == 0)
            menu_ffb_effects.push_back("NEXT PAGE");
        else
            menu_ffb_effects.push_back("PREV PAGE");

        menu_ffb_effects.push_back(ENTRY_BACK);
    }

    void adjust_ffb_effect_dx(int delta)
    {
        int item_count = 0;
        const FfbMenuItem* items = dx_ffb_effect_items(item_count);
        const int page_size = 9;
        const int index = ffb_effect_page * page_size + cursor;
        if (index < 0 || index >= item_count)
            return;

        const std::string setting = items[index].setting;
        if (setting == "engine_speed")
        {
            int value = 50 + config.ffb_effect_setting("engine_speed", 50);

            // Speed is intentionally coarser than effect strength. A 1% change
            // is effectively inaudible/tactile here, so LEFT/RIGHT and Enter all
            // move in useful 5% steps across the 50..150% range.
            value += delta < 0 ? -5 : 5;
            value = std::max(50, std::min(150, value));
            config.set_ffb_effect_setting("engine_speed", value - 50);
        }
        else
        {
            int value = config.ffb_effect_setting(items[index].setting, 0) + delta;
            value = std::max(0, std::min(100, value));
            config.set_ffb_effect_setting(items[index].setting, value);
        }

        if (!config.save())
            display_message("ERROR SAVING SETTINGS!");
        populate_ffb_effects_dx();
    }

    void tick_ffb_tuning_menu_dx()
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
                populate_ffb_effects_dx();
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
            const int item_count = 18;
            const int visible_items =
                std::min(page_size, item_count - ffb_effect_page * page_size);

            if (cursor < visible_items && (delta != 0 || selected))
            {
                adjust_ffb_effect_dx(delta != 0 ? delta : 5);
                return;
            }

            if (!selected)
                return;

            if (cursor == visible_items)
            {
                ffb_effect_page = ffb_effect_page == 0 ? 1 : 0;
                populate_ffb_effects_dx();
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
        menu_endless.push_back(
            std::string("FIRST STAGE ") +
            (config.endless_random_start() ? "RANDOM" : "COCONUT"));
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
                config.set_endless_random_start(!config.endless_random_start());
                break;

            case 1:
                config.set_endless_start_time(
                    adjust_time_value(config.endless_start_time(), direction, 30));
                break;

            case 2:
                config.set_endless_checkpoint_time(
                    adjust_time_value(config.endless_checkpoint_time(), direction, 20));
                break;

            case 3:
            {
                int value = config.endless_time_decrease() + direction;
                if (value < 0) value = 0;
                if (value > 10) value = 10;
                config.set_endless_time_decrease(value);
                break;
            }

            case 4:
            {
                int value = config.endless_time_interval() + direction;
                if (value < 1) value = 1;
                if (value > 10) value = 10;
                config.set_endless_time_interval(value);
                break;
            }

            case 5:
                config.set_endless_min_checkpoint(
                    adjust_time_value(config.endless_min_checkpoint(), direction, 10));
                break;

            case 6:
            {
                int value = config.endless_start_traffic() + direction;
                if (value < 0) value = 0;
                if (value > 8) value = 8;
                config.set_endless_start_traffic(value);
                break;
            }

            case 7:
            {
                int value = config.endless_traffic_increase() + direction;
                if (value < 0) value = 0;
                if (value > 8) value = 8;
                config.set_endless_traffic_increase(value);
                break;
            }

            case 8:
            {
                int value = config.endless_traffic_interval() + direction;
                if (value < 1) value = 1;
                if (value > 10) value = 10;
                config.set_endless_traffic_interval(value);
                break;
            }

            case 9:
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
        // Once inside the FFB tuning pages, use the DX handler so the engine
        // sine speed can live beside the existing effect strengths without
        // putting another option on the main Controls page.
        if (menu_selected == &menu_ffb_tuning ||
            menu_selected == &menu_ffb_effects ||
            menu_selected == &menu_ffb_spring)
        {
            tick_ffb_tuning_menu_dx();
            return;
        }

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

            if (cursor >= 0 && cursor <= 9)
            {
                if (adjust_endless_value(cursor, 1))
                {
                    config.save();
                    osoundint.queue_sound(sound::BEEP1);
                }
                return;
            }

            if (cursor == 10)
            {
                config.reset_endless_settings();
                config.save();
                populate_endless_settings();
                cursor = 10;
                osoundint.queue_sound(sound::BEEP1);
                return;
            }

            if (cursor == 11)
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
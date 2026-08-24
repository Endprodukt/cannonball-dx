/***************************************************************************
    Time Trial Mode Front End.

    This file is part of Cannonball.
    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <SDL.h>
#include <string>

#include "sdl2/input.hpp"

#include "frontend/ttrial.hpp"
#include "../utils.hpp"

#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/omusic.hpp"
#include "engine/outils.hpp"
#include "engine/omap.hpp"
#include "engine/ostats.hpp"
#include "engine/otiles.hpp"

// Track Selection: Ferrari Position Per Track
// This is a link to a sprite object that represents part of the course map.
static const uint8_t FERRARI_POS[] =
{
    1,5,3,11,9,7,19,17,15,13,24,23,22,21,20
};

// Map Stage Number to Internal Lookup
static const uint8_t STAGE_LOOKUP[] =
{
    0x00,
    0x09, 0x08,
    0x12, 0x11, 0x10,
    0x1B, 0x1A, 0x19, 0x18,
    0x24, 0x23, 0x22, 0x21, 0x20
};

namespace
{
    const int TRAFFIC_OFF = 0;
    const int TRAFFIC_ON = 1;

    struct CourseRecordDisplay
    {
        uint16_t total_counter = 0;
        char initial1 = ' ';
        char initial2 = ' ';
        char initial3 = ' ';
    };

    CourseRecordDisplay course_records[2][15];
    uint16_t fastest_laps[2][15] = {};
    Uint32 course_select_deadline_ms = 0;

    // Keep the last chosen traffic class for the next selector visit. The first
    // visit starts with the traditional Time Trial behaviour: traffic enabled.
    bool selector_active = false;
    bool traffic_enabled = true;

    int traffic_class()
    {
        return traffic_enabled ? TRAFFIC_ON : TRAFFIC_OFF;
    }

    const char* traffic_name()
    {
        return traffic_enabled ? "TRAFFIC ON" : "TRAFFIC OFF";
    }

    Uint32 selection_timeout_ms()
    {
        const int seconds = config.selection_timer_seconds();
        return seconds > 0 ? static_cast<Uint32>(seconds) * 1000U : 0;
    }

    bool course_selection_timed_out()
    {
        return course_select_deadline_ms != 0 &&
            static_cast<Sint32>(SDL_GetTicks() - course_select_deadline_ms) >= 0;
    }

    const char* track_name(int index)
    {
        switch (index)
        {
            case 0:  return "COCONUT BEACH";
            case 1:  return config.engine.jap ? "WHEAT FIELD" : "GATEWAY";
            case 2:  return config.engine.jap ? "CLOUDY MOUNTAIN" : "DEVILS CANYON";
            case 3:  return "DESERT";
            case 4:  return "ALPS";
            case 5:  return config.engine.jap ? "DEVILS CANYON" : "CLOUDY MOUNTAIN";
            case 6:  return "WILDERNESS";
            case 7:  return "OLD CAPITAL";
            case 8:  return config.engine.jap ? "GATEWAY" : "WHEAT FIELD";
            case 9:  return "SEASIDE TOWN";
            case 10: return "VINEYARD";
            case 11: return "DEATH VALLEY";
            case 12: return "DESOLATION HILL";
            case 13: return "AUTOBAHN";
            case 14: return "LAKESIDE";
            default: return "UNKNOWN";
        }
    }

    void read_record(xml_parser::ptree& data,
                     const std::string& tag,
                     CourseRecordDisplay& record)
    {
        record.total_counter = static_cast<uint16_t>(
            data.get_int(tag + ".total", 0));

        const std::string i1 = data.get_string(tag + ".initial1", "_");
        const std::string i2 = data.get_string(tag + ".initial2", "_");
        const std::string i3 = data.get_string(tag + ".initial3", "_");

        record.initial1 = i1.empty() || i1[0] == '_' ? ' ' : i1[0];
        record.initial2 = i2.empty() || i2[0] == '_' ? ' ' : i2[0];
        record.initial3 = i3.empty() || i3[0] == '_' ? ' ' : i3[0];
    }

    void load_course_records(const uint16_t* legacy_best_times)
    {
        for (int mode = 0; mode < 2; mode++)
        {
            for (int track = 0; track < 15; track++)
            {
                course_records[mode][track] = CourseRecordDisplay{};
                fastest_laps[mode][track] = 0;
            }
        }

        xml_parser::ptree data("timetrial_scores");
        const std::string filename = config.engine.jap ?
            config.data.file_ttrial_jap : config.data.file_ttrial;

        if (!xml_parser::read_xml(filename, data))
            return;

        for (int track = 0; track < 15; track++)
        {
            const std::string track_tag =
                "time_trial.track" + Utils::to_string(track);

            for (int mode = 0; mode < 2; mode++)
            {
                const std::string class_tag = track_tag +
                    (mode == TRAFFIC_ON ? ".traffic_on" : ".traffic_off");

                fastest_laps[mode][track] = static_cast<uint16_t>(
                    data.get_int(class_tag + ".fastest_lap", 0));

                read_record(
                    data,
                    class_tag + ".entry0",
                    course_records[mode][track]);
            }

            // Compatibility with every Time Trial XML written before the
            // multi-entry split. Those records were always the traditional
            // traffic-enabled class, so migrate them into TRAFFIC ON only.
            if (!fastest_laps[TRAFFIC_ON][track])
            {
                fastest_laps[TRAFFIC_ON][track] = static_cast<uint16_t>(
                    data.get_int(
                        "time_trial.score" + Utils::to_string(track),
                        legacy_best_times ? legacy_best_times[track] : 0));
            }

            if (!course_records[TRAFFIC_ON][track].total_counter)
            {
                read_record(
                    data,
                    "time_trial.record" + Utils::to_string(track),
                    course_records[TRAFFIC_ON][track]);
            }
        }
    }

    void draw_course_record(int level_selected)
    {
        if (level_selected < 0 || level_selected >= 15)
            return;

        const CourseRecordDisplay& record =
            course_records[traffic_class()][level_selected];

        // Prominently identify the course currently selected on the map.
        ohud.blit_text_big(1, track_name(level_selected));

        // Make the Traffic control explicit without involving VIEW2/VIEW3.
        // The two-line block is right-aligned in the free top-right area.
        ohud.blit_text_new(30, 3, "PRESS VIEW", OHud::GREY);
        ohud.blit_text_new(26, 4, "TRAFFIC ON/OFF", OHud::GREEN);

        // Keep all Time Trial record information on one compact line below
        // the map. The top entry is always from the selected traffic class.
        ohud.blit_text_new(0, 26, "                                        ", OHud::GREY);
        ohud.blit_text_new(1, 26, "RECORD", OHud::GREY);
        ohud.blit_text_new(20, 26, "FASTEST LAP", OHud::GREY);

        char initials[4] =
        {
            record.initial1 == ' ' ? '-' : record.initial1,
            record.initial2 == ' ' ? '-' : record.initial2,
            record.initial3 == ' ' ? '-' : record.initial3,
            0
        };
        ohud.blit_text_new(8, 26, initials, OHud::GREEN);

        if (!record.total_counter)
        {
            ohud.blit_text_new(12, 26, "NO TIME", OHud::GREEN);
            return;
        }

        uint8_t converted[3] = {0, 0, 0};
        outils::convert_counter_to_time(record.total_counter, converted);
        ohud.draw_lap_timer(
            ohud.translate(12, 26),
            converted,
            converted[2]);
    }
}

bool time_trial_selector_active()
{
    return selector_active;
}

bool time_trial_selector_traffic_enabled()
{
    return traffic_enabled;
}

TTrial::TTrial(uint16_t* best_times)
{
    this->best_times = best_times;
}

TTrial::~TTrial(void)
{
}

void TTrial::init()
{
    course_select_deadline_ms = 0;
    selector_active = true;
    state = INIT_COURSEMAP;
}

int TTrial::tick()
{
    switch (state)
    {
        case INIT_COURSEMAP:
            outrun.select_course(config.engine.jap != 0, config.engine.prototype != 0);
            config.load_timetrial_scores();
            load_course_records(best_times);
            ostats.init(true);
            osprites.init();
            video.enabled = true;
            video.sprite_layer->set_x_clip(false);
            omap.init();
            omap.load_sprites();
            omap.position_ferrari(FERRARI_POS[level_selected = 0]);

            ohud.blit_text_new(0, 21, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 23, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 25, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 26, "                                        ", OHud::GREY);
            osoundint.queue_sound(sound::PCM_WAVE);
            outrun.ttrial.laps = config.ttrial.laps;

            {
                const Uint32 timeout_ms = selection_timeout_ms();
                course_select_deadline_ms = timeout_ms
                    ? SDL_GetTicks() + timeout_ms
                    : 0;
            }
            state = TICK_COURSEMAP;
            [[fallthrough]];

        case TICK_COURSEMAP:
            {
                if (input.has_pressed(Input::MENU))
                {
                    course_select_deadline_ms = 0;
                    selector_active = false;
                    omusic.cancel_time_trial_from_music();
                    return BACK_TO_MENU;
                }

                const bool timed_out = course_selection_timed_out();

                if (timed_out ||
                    input.has_pressed(Input::START) ||
                    input.has_pressed(Input::ACCEL) ||
                    oinputs.is_analog_select())
                {
                    course_select_deadline_ms = 0;
                    selector_active = false;

                    const int mode = traffic_class();
                    const uint16_t selected_best =
                        fastest_laps[mode][level_selected];
                    const uint16_t target_counter =
                        selected_best ? selected_best : 10000;
                    outils::convert_counter_to_time(
                        target_counter,
                        best_converted);

                    const uint8_t selected_traffic = static_cast<uint8_t>(
                        traffic_enabled ? config.ttrial.traffic : 0);

                    outrun.cannonball_mode         = Outrun::MODE_TTRIAL;
                    outrun.ttrial.level            = STAGE_LOOKUP[level_selected];
                    outrun.ttrial.traffic          = selected_traffic;
                    outrun.custom_traffic          = selected_traffic;
                    outrun.ttrial.current_lap      = 0;
                    outrun.ttrial.best_lap_counter = target_counter;
                    outrun.ttrial.best_lap[0]      = best_converted[0];
                    outrun.ttrial.best_lap[1]      = best_converted[1];
                    outrun.ttrial.best_lap[2]      = best_converted[2];
                    outrun.ttrial.new_high_score   = false;
                    outrun.ttrial.overtakes        = 0;
                    outrun.ttrial.crashes          = 0;
                    outrun.ttrial.vehicle_cols     = 0;
                    ostats.credits = 1;
                    return INIT_GAME;
                }

                // Both supported cabinet styles use one simple toggle: either
                // the dedicated VIEW1 button or the classic/general VIEW button.
                // VIEW2 and VIEW3 remain completely unrelated to Traffic.
                if (input.has_pressed(Input::VIEW1) ||
                    input.has_pressed(Input::VIEWPOINT))
                {
                    traffic_enabled = !traffic_enabled;
                    osoundint.queue_sound(sound::BEEP1);
                }

                const int previous_level = level_selected;

                if (input.has_pressed(Input::LEFT) || oinputs.is_analog_l())
                {
                    if (--level_selected < 0)
                        level_selected = sizeof(FERRARI_POS) - 1;
                }
                else if (input.has_pressed(Input::RIGHT) || oinputs.is_analog_r())
                {
                    if (++level_selected > sizeof(FERRARI_POS) - 1)
                        level_selected = 0;
                }

                if (level_selected != previous_level)
                    osoundint.queue_sound(sound::BEEP1);

                omap.position_ferrari(FERRARI_POS[level_selected]);

                const int mode = traffic_class();
                const uint16_t selected_best =
                    fastest_laps[mode][level_selected];

                draw_course_record(level_selected);

                if (selected_best)
                {
                    outils::convert_counter_to_time(
                        selected_best,
                        best_converted);
                    ohud.draw_lap_timer(
                        ohud.translate(32, 26),
                        best_converted,
                        best_converted[2]);
                }
                else
                {
                    ohud.blit_text_new(32, 26, "NO TIME", OHud::GREEN);
                }

                omap.blit();
                oroad.tick();
                osprites.sprite_copy();
                osprites.update_sprites();
                otiles.write_tilemap_hw();
                otiles.update_tilemaps(0);
            }
            break;
    }

    return CONTINUE;
}

void TTrial::update_best_time()
{
    // Retain the legacy traffic-enabled best-lap writer for compatibility with
    // older flows. The DX multi-table path persists both classes itself.
    if (outrun.ttrial.traffic)
    {
        best_times[level_selected] = outrun.ttrial.best_lap_counter;
        config.save_timetrial_scores();
    }
}
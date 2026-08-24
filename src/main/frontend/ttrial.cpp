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
    struct CourseRecordDisplay
    {
        uint16_t total_counter = 0;
        char initial1 = ' ';
        char initial2 = ' ';
        char initial3 = ' ';
    };

    CourseRecordDisplay course_records[15];
    Uint32 course_select_deadline_ms = 0;

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

    void load_course_records()
    {
        for (CourseRecordDisplay& record : course_records)
            record = CourseRecordDisplay{};

        xml_parser::ptree data("timetrial_scores");
        const std::string filename = config.engine.jap ?
            config.data.file_ttrial_jap : config.data.file_ttrial;

        if (!xml_parser::read_xml(filename, data))
            return;

        for (int i = 0; i < 15; i++)
        {
            const std::string tag =
                "time_trial.record" + Utils::to_string(i);

            CourseRecordDisplay& record = course_records[i];
            record.total_counter = static_cast<uint16_t>(
                data.get_int(tag + ".total", 0));

            const std::string i1 = data.get_string(tag + ".initial1", "_");
            const std::string i2 = data.get_string(tag + ".initial2", "_");
            const std::string i3 = data.get_string(tag + ".initial3", "_");

            record.initial1 = i1.empty() || i1[0] == '_' ? ' ' : i1[0];
            record.initial2 = i2.empty() || i2[0] == '_' ? ' ' : i2[0];
            record.initial3 = i3.empty() || i3[0] == '_' ? ' ' : i3[0];
        }
    }

    void draw_course_record(int level_selected)
    {
        if (level_selected < 0 || level_selected >= 15)
            return;

        const CourseRecordDisplay& record = course_records[level_selected];

        // Prominently identify the course currently selected on the map.
        ohud.blit_text_big(1, track_name(level_selected));
        ohud.blit_text_new(9, 4, "STEER TO SELECT TRACK", OHud::GREY);

        // Keep all Time Trial record information on one compact line below
        // the map. The old two-row LAP graphic is intentionally omitted.
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
    state = INIT_COURSEMAP;
}

int TTrial::tick()
{
    switch (state)
    {
        case INIT_COURSEMAP:
            outrun.select_course(config.engine.jap != 0, config.engine.prototype != 0); // Need to setup correct course map graphics.
            config.load_timetrial_scores();
            load_course_records();
            ostats.init(true);
            osprites.init();
            video.enabled = true;
            video.sprite_layer->set_x_clip(false);
            omap.init();
            omap.load_sprites();
            omap.position_ferrari(FERRARI_POS[level_selected = 0]);
            // Clear the lower text area previously occupied by the two-row
            // LAP label and by the earlier stacked Course Record layout.
            ohud.blit_text_new(0, 21, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 23, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 25, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 26, "                                        ", OHud::GREY);
            osoundint.queue_sound(sound::PCM_WAVE);
            outrun.ttrial.laps    = config.ttrial.laps;
            outrun.custom_traffic = config.ttrial.traffic;

            // The selector shares the same Game Engine duration as Music
            // Select: 15 seconds, 30 seconds, or unlimited when set to OFF.
            {
                const Uint32 timeout_ms = selection_timeout_ms();
                course_select_deadline_ms = timeout_ms
                    ? SDL_GetTicks() + timeout_ms
                    : 0;
            }
            state = TICK_COURSEMAP;

        case TICK_COURSEMAP:
            {
                if (input.has_pressed(Input::MENU))
                {
                    course_select_deadline_ms = 0;
                    omusic.cancel_time_trial_from_music();
                    return BACK_TO_MENU;
                }

                // Match Music Select: once the configured selection duration
                // expires, accept whatever is currently highlighted. Check the
                // deadline before directional input so a held wheel cannot keep
                // the selector alive indefinitely after timeout.
                const bool timed_out = course_selection_timed_out();

                if (timed_out ||
                    input.has_pressed(Input::START) ||
                    input.has_pressed(Input::ACCEL) ||
                    oinputs.is_analog_select())
                {
                    course_select_deadline_ms = 0;
                    outils::convert_counter_to_time(best_times[level_selected], best_converted);

                    outrun.cannonball_mode         = Outrun::MODE_TTRIAL;
                    outrun.ttrial.level            = STAGE_LOOKUP[level_selected];
                    outrun.ttrial.current_lap      = 0;
                    outrun.ttrial.best_lap_counter = 10000;
                    outrun.ttrial.best_lap[0]      = best_converted[0];
                    outrun.ttrial.best_lap[1]      = best_converted[1];
                    outrun.ttrial.best_lap[2]      = best_converted[2];
                    outrun.ttrial.best_lap_counter = best_times[level_selected];
                    outrun.ttrial.new_high_score   = false;
                    outrun.ttrial.overtakes        = 0;
                    outrun.ttrial.crashes          = 0;
                    outrun.ttrial.vehicle_cols     = 0;
                    ostats.credits = 1;
                    return INIT_GAME;
                }

                const int previous_level = level_selected;

                if (input.has_pressed(Input::LEFT) || oinputs.is_analog_l())
                {
                    if (--level_selected < 0)
                        level_selected = sizeof(FERRARI_POS) - 1;
                }
                else if (input.has_pressed(Input::RIGHT)|| oinputs.is_analog_r())
                {
                    if (++level_selected > sizeof(FERRARI_POS) - 1)
                        level_selected = 0;
                }

                // Give every actual course change a short arcade selection cue.
                // This follows the same BEEP1 language already used elsewhere
                // in the DX selection screens and also sounds on wrap-around.
                if (level_selected != previous_level)
                    osoundint.queue_sound(sound::BEEP1);

                omap.position_ferrari(FERRARI_POS[level_selected]);

                // Course Record total and the existing absolute best single
                // lap are presented side-by-side on the same bottom row.
                outils::convert_counter_to_time(best_times[level_selected], best_converted);
                draw_course_record(level_selected);
                ohud.draw_lap_timer(
                    ohud.translate(32, 26),
                    best_converted,
                    best_converted[2]);

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
    best_times[level_selected] = outrun.ttrial.best_lap_counter;
    config.save_timetrial_scores();
}

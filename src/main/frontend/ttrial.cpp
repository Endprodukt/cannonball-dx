/***************************************************************************
    Time Trial Mode Front End.

    This file is part of Cannonball. 
    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

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

        // The original single-lap record remains at rows 25/26. The new
        // three-lap record that actually determines the DX Course Record is
        // displayed directly above it together with the record holder.
        ohud.blit_text_new(0, 21, "                                        ", OHud::GREY);
        ohud.blit_text_new(0, 23, "                                        ", OHud::GREY);
        ohud.blit_text_new(2, 21, "3 LAP RECORD", OHud::GREY);
        ohud.blit_text_new(2, 23, "RECORD HOLDER", OHud::GREY);

        if (!record.total_counter)
        {
            ohud.blit_text_new(18, 21, "NO RECORD", OHud::GREEN);
            ohud.blit_text_new(18, 23, "---", OHud::GREEN);
            return;
        }

        uint8_t converted[3] = {0, 0, 0};
        outils::convert_counter_to_time(record.total_counter, converted);
        ohud.draw_lap_timer(
            ohud.translate(18, 21),
            converted,
            converted[2]);

        char initials[4] =
        {
            record.initial1 == ' ' ? '-' : record.initial1,
            record.initial2 == ' ' ? '-' : record.initial2,
            record.initial3 == ' ' ? '-' : record.initial3,
            0
        };
        ohud.blit_text_new(18, 23, initials, OHud::GREEN);
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
            ohud.blit_text1(2, 25, TEXT1_LAPTIME1);
            ohud.blit_text1(2, 26, TEXT1_LAPTIME2);
            osoundint.queue_sound(sound::PCM_WAVE);
            outrun.ttrial.laps    = config.ttrial.laps;
            outrun.custom_traffic = config.ttrial.traffic;
            state = TICK_COURSEMAP;

        case TICK_COURSEMAP:
            {
                if (input.has_pressed(Input::MENU))
                {
                    omusic.cancel_time_trial_from_music();
                    return BACK_TO_MENU;
                }
                else if (input.has_pressed(Input::LEFT) || oinputs.is_analog_l())
                {
                    if (--level_selected < 0)
                        level_selected = sizeof(FERRARI_POS) - 1;
                }
                else if (input.has_pressed(Input::RIGHT)|| oinputs.is_analog_r())
                {
                    if (++level_selected > sizeof(FERRARI_POS) - 1)
                        level_selected = 0;
                }
                else if (input.has_pressed(Input::START) || input.has_pressed(Input::ACCEL) || oinputs.is_analog_select())
                {
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

                omap.position_ferrari(FERRARI_POS[level_selected]);

                // Keep the existing absolute best single lap visible for
                // reference, but make the new three-lap record and holder the
                // primary Time Trial target on this screen.
                outils::convert_counter_to_time(best_times[level_selected], best_converted);
                ohud.draw_lap_timer(
                    ohud.translate(7, 26),
                    best_converted,
                    best_converted[2]);
                draw_course_record(level_selected);

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

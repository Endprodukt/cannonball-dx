/***************************************************************************
    Time Trial Mode Front End.

    This file is part of Cannonball.
    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <SDL.h>
#include <string>
#include <algorithm>

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

    const int COURSE_ROW_START[] = {0, 1, 3, 6, 10};
    const int COURSE_ROW_COUNT[] = {1, 2, 3, 4, 5};
    const int COURSE_ROWS = 5;

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

    // Digital navigation follows the actual branching course tree. Remember
    // the route taken through each depth so LEFT can reverse the exact RIGHT
    // movement even at stages shared by two possible parents.
    int digital_route[COURSE_ROWS] = {-1, -1, -1, -1, -1};

    int traffic_class()
    {
        return traffic_enabled ? TRAFFIC_ON : TRAFFIC_OFF;
    }

    const char* traffic_name()
    {
        return traffic_enabled ? "TRAFFIC ON" : "TRAFFIC OFF";
    }

    int course_row(int index)
    {
        for (int row = COURSE_ROWS - 1; row > 0; --row)
        {
            if (index >= COURSE_ROW_START[row])
                return row;
        }
        return 0;
    }

    int move_course_lane(int current, int direction)
    {
        const int row = course_row(current);
        const int row_start = COURSE_ROW_START[row];
        const int row_end = row_start + COURSE_ROW_COUNT[row] - 1;
        const int next = current + direction;

        return next >= row_start && next <= row_end ? next : current;
    }

    int course_x_distance(int first, int second)
    {
        int distance =
            osprites.jump_table[FERRARI_POS[first]].x -
            osprites.jump_table[FERRARI_POS[second]].x;
        return distance < 0 ? -distance : distance;
    }

    bool course_is_child(int parent, int child)
    {
        if (parent < 0 || parent >= 15 || child < 0 || child >= 15)
            return false;

        const int parent_row = course_row(parent);
        const int child_row = course_row(child);
        if (child_row != parent_row + 1)
            return false;

        const int parent_column = parent - COURSE_ROW_START[parent_row];
        const int child_column = child - COURSE_ROW_START[child_row];
        return child_column == parent_column ||
               child_column == parent_column + 1;
    }

    int nearest_child(int current)
    {
        const int row = course_row(current);
        if (row >= COURSE_ROWS - 1)
            return current;

        const int column = current - COURSE_ROW_START[row];
        const int first = COURSE_ROW_START[row + 1] + column;
        const int second = first + 1;

        return course_x_distance(current, first) <=
               course_x_distance(current, second) ? first : second;
    }

    int nearest_parent(int current)
    {
        const int row = course_row(current);
        if (row <= 0)
            return current;

        const int column = current - COURSE_ROW_START[row];
        const int previous_start = COURSE_ROW_START[row - 1];
        const int previous_count = COURSE_ROW_COUNT[row - 1];

        if (column == 0)
            return previous_start;
        if (column == previous_count)
            return previous_start + previous_count - 1;

        const int first = previous_start + column - 1;
        const int second = previous_start + column;
        return course_x_distance(current, first) <=
               course_x_distance(current, second) ? first : second;
    }

    void forget_digital_route_after(int row)
    {
        for (int i = row + 1; i < COURSE_ROWS; ++i)
            digital_route[i] = -1;
    }

    void reset_digital_route(int current)
    {
        std::fill(
            digital_route,
            digital_route + COURSE_ROWS,
            -1);
        digital_route[course_row(current)] = current;
    }

    void remember_digital_lane(int current)
    {
        const int row = course_row(current);
        digital_route[row] = current;
        forget_digital_route_after(row);
    }

    int move_course_forward(int current)
    {
        const int row = course_row(current);
        if (row >= COURSE_ROWS - 1)
            return current;

        const int remembered = digital_route[row + 1];
        if (digital_route[row] == current &&
            course_is_child(current, remembered))
        {
            return remembered;
        }

        const int next = nearest_child(current);
        digital_route[row] = current;
        digital_route[row + 1] = next;
        forget_digital_route_after(row + 1);
        return next;
    }

    int move_course_back(int current)
    {
        const int row = course_row(current);
        if (row <= 0)
            return current;

        const int remembered = digital_route[row - 1];
        if (digital_route[row] == current &&
            course_is_child(remembered, current))
        {
            return remembered;
        }

        const int previous = nearest_parent(current);
        digital_route[row] = current;
        digital_route[row - 1] = previous;
        return previous;
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

    void clear_selector_widescreen_margins()
    {
        if (!video.pixels || config.s16_x_off <= 0)
            return;

        // OMap::init() paints the map background with 0xABD through the solid
        // tile at palette pixel 0x261. The 16:9/21:9 margins are not fully
        // overwritten by the System 16 tile/road layers, so pixels from the
        // preceding Music Select side art can otherwise remain in those areas.
        // Clear only the margins; map sprites (especially the extended sea)
        // are rendered afterwards and remain completely untouched.
        constexpr uint16_t MAP_BACKGROUND_PIXEL = 0x261;
        const int scale = std::clamp(config.video.hires + 1, 1, 4);
        const int left_margin = config.s16_x_off * scale;
        const int centre_width = S16_WIDTH * scale;
        const int right_start = left_margin + centre_width;
        const int width = config.s16_width;
        const int height = config.s16_height;

        for (int y = 0; y < height; y++)
        {
            uint16_t* row = video.pixels + (y * width);

            for (int x = 0; x < left_margin; x++)
                row[x] = MAP_BACKGROUND_PIXEL;

            for (int x = right_start; x < width; x++)
                row[x] = MAP_BACKGROUND_PIXEL;
        }
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

        // Align the two-line Traffic control with the two-row course title.
        // The status is live: VIEW toggles between TRAFFIC ON and TRAFFIC OFF.
        ohud.blit_text_new(30, 1, "PRESS VIEW", OHud::GREY);
        ohud.blit_text_new(
            traffic_enabled ? 30 : 29,
            2,
            traffic_name(),
            OHud::GREEN);

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
            // Keep the normal widescreen course-map sprites enabled. The sea
            // extension is intentional; stale Music Select pixels are cleared
            // independently in clear_selector_widescreen_margins().
            video.sprite_layer->set_x_clip(false);
            omap.init();
            omap.load_sprites();
            omap.position_ferrari(FERRARI_POS[level_selected = 0]);
            reset_digital_route(level_selected);

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

                // The course map is a branching tree, not a rectangular grid.
                // RIGHT follows one real outgoing branch. LEFT walks back along
                // the exact branch previously taken, while UP/DOWN move between
                // neighbouring courses at the same depth. A real analog wheel
                // deliberately keeps the original linear 0..14 selector.
                if (input.has_pressed(Input::RIGHT))
                {
                    level_selected = move_course_forward(level_selected);
                }
                else if (input.has_pressed(Input::LEFT))
                {
                    level_selected = move_course_back(level_selected);
                }
                else if (input.has_pressed(Input::UP))
                {
                    level_selected = move_course_lane(level_selected, -1);
                    remember_digital_lane(level_selected);
                }
                else if (input.has_pressed(Input::DOWN))
                {
                    level_selected = move_course_lane(level_selected, 1);
                    remember_digital_lane(level_selected);
                }
                else if (!input.is_pressed(Input::LEFT) &&
                         !input.is_pressed(Input::RIGHT) &&
                         !input.is_pressed(Input::UP) &&
                         !input.is_pressed(Input::DOWN))
                {
                    if (oinputs.is_analog_l())
                    {
                        if (--level_selected < 0)
                            level_selected = sizeof(FERRARI_POS) - 1;
                        reset_digital_route(level_selected);
                    }
                    else if (oinputs.is_analog_r())
                    {
                        if (++level_selected > sizeof(FERRARI_POS) - 1)
                            level_selected = 0;
                        reset_digital_route(level_selected);
                    }
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

                clear_selector_widescreen_margins();
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
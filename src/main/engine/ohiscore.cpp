/***************************************************************************
    Best Outrunners - CannonBall DX score-screen wrapper.

    The original score implementation is preserved in ohiscore_base.cpp.
    Endless and Time Trial substitute their dedicated tables only at game end.

    DX attract mode uses one common Best OutRunners presentation for every
    score category: the original title, a mode label, column headers and five
    visible entries. The stock mini-car reveal is retained, but custom tables
    use their own stable field palettes instead of the original position-based
    Route/Name/Record palette assumptions.
***************************************************************************/

#include "main.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/oroad.hpp"
#include "engine/ostats.hpp"
#include "engine/outils.hpp"
#include "engine/ohiscore.hpp"
#include "engine/oinitengine.hpp"
#include "engine/opalette.hpp"
#include "engine/otiles.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <string>

#include "engine/endless_hiscore.hpp"
#include "engine/time_trial_records.hpp"

#define init init_base
#define tick tick_base
#define display_scores display_scores_base
#define score_position score_position_base
#include "ohiscore_base.cpp"
#undef score_position
#undef display_scores
#undef tick
#undef init

EndlessHiScore endless_hiscore;
TimeTrialRecords time_trial_records;

namespace
{
    enum AttractScorePage
    {
        ATTRACT_SCORE_ORIGINAL = 0,
        ATTRACT_SCORE_ORIGINAL_JAPAN,
        ATTRACT_SCORE_CONTINUOUS,
        ATTRACT_SCORE_ENDLESS,
        ATTRACT_SCORE_TTRIAL_WITH_1,
        ATTRACT_SCORE_TTRIAL_WITH_2,
        ATTRACT_SCORE_TTRIAL_WITH_3,
        ATTRACT_SCORE_TTRIAL_WITHOUT_1,
        ATTRACT_SCORE_TTRIAL_WITHOUT_2,
        ATTRACT_SCORE_TTRIAL_WITHOUT_3,
        ATTRACT_SCORE_PAGE_COUNT
    };

    const int ATTRACT_SCORE_SECONDS = 5;
    const int ATTRACT_VISIBLE_ROWS = 5;
    const uint16_t ATTRACT_ROW_BOTTOM_Y = 10;

    bool endless_score_audio_started = false;
    bool time_trial_score_audio_started = false;

    bool attract_score_rotation_active = false;
    int attract_score_page = ATTRACT_SCORE_ORIGINAL;
    int attract_saved_mode = Outrun::MODE_ORIGINAL;
    int attract_saved_jap = 0;

    bool endless_gameover_score_screen()
    {
        return outrun.endless_mode &&
               outrun.cannonball_mode == Outrun::MODE_CONT &&
               (outrun.game_state == GS_INIT_BEST2 ||
                outrun.game_state == GS_BEST2);
    }

    bool time_trial_record_screen()
    {
        return outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
               (outrun.game_state == GS_INIT_BEST2 ||
                outrun.game_state == GS_BEST2);
    }

    bool attract_score_screen()
    {
        return outrun.game_state == GS_INIT_BEST1 ||
               outrun.game_state == GS_BEST1;
    }

    bool attract_time_trial_page(int page)
    {
        return page >= ATTRACT_SCORE_TTRIAL_WITH_1 &&
               page <= ATTRACT_SCORE_TTRIAL_WITHOUT_3;
    }

    bool attract_time_trial_with_traffic(int page)
    {
        return page >= ATTRACT_SCORE_TTRIAL_WITH_1 &&
               page <= ATTRACT_SCORE_TTRIAL_WITH_3;
    }

    int attract_time_trial_page_index(int page)
    {
        if (attract_time_trial_with_traffic(page))
            return page - ATTRACT_SCORE_TTRIAL_WITH_1;

        return page - ATTRACT_SCORE_TTRIAL_WITHOUT_1;
    }

    void stabilize_score_background()
    {
        oroad.init();
        oroad.stage_lookup_off = 0;
        oinitengine.init_road_seg_master();

        otiles.init();
        otiles.reset_tiles_pal();
        otiles.setup_palette_hud();
        otiles.setup_palette_tilemap();
        otiles.update_tilemaps(0);
        otiles.write_tilemap_hw();

        opalette.setup_sky_palette();
        opalette.setup_ground_color();
        opalette.setup_road_centre();
        opalette.setup_road_stripes();
        opalette.setup_road_side();
        opalette.setup_road_colour();

        ohiscore.setup_pal_best();
        ohiscore.setup_road_best();

        oroad.set_view_mode(ORoad::VIEW_ORIGINAL, true);
        oroad.horizon_base = 0x154;
        oroad.horizon_set = 1;
        oroad.road_pos = 0;
        oroad.road_pos_change = 0;
        oroad.tilemap_h_target = 0;
    }

    uint8_t attract_tile_char(char value)
    {
        if (value == '\'')
            return 0x5E;
        if (value == '"')
            return 0x5F;
        if (value == '.')
            return 0x5B;
        return static_cast<uint8_t>(value);
    }

    void draw_attract_text(uint16_t x,
                           uint16_t y,
                           const std::string& text,
                           uint16_t colour)
    {
        uint32_t dst = ohud.translate(x, y);

        for (char value : text)
        {
            if (value == ' ')
                video.write_text16(&dst, 0);
            else
                video.write_text16(
                    &dst,
                    (colour << 8) | attract_tile_char(value));
        }
    }

    void draw_attract_centered(uint16_t y,
                               const std::string& text,
                               uint16_t colour)
    {
        int x = 20 - static_cast<int>(text.size() / 2);
        if (x < 0)
            x = 0;

        draw_attract_text(static_cast<uint16_t>(x), y, text, colour);
    }

    void draw_best_outrunners_title()
    {
        uint32_t src = TEXT2_BEST_OR;
        uint32_t dst = 0x110000 + roms.rom0.read16(&src) - 0x80;

        uint16_t pal = roms.rom0.read8(&src);
        pal = 0x80A0 | ((pal << 9) | (pal >> 7) & 1);
        const uint16_t counter = roms.rom0.read8(&src);

        for (uint16_t i = 0; i <= counter; i++)
        {
            uint16_t data = roms.rom0.read8(&src);

            if (data == 0x20)
            {
                video.write_text16(&dst, 0);
                video.write_text16(0x7E + dst, 0);
                continue;
            }

            data -= 0x41;
            data = (data * 2) + pal;
            video.write_text16(&dst, data);
            video.write_text16(0x7E + dst, data + 1);
        }
    }

    void clear_score_tile_page()
    {
        uint32_t tile_addr = 0x10E000;
        for (int i = 0; i <= 0x3FF; i++)
            video.write_tile32(&tile_addr, 0x200020);
    }

    uint32_t attract_tile_address(uint16_t x, uint16_t y)
    {
        return ohud.translate(
            static_cast<uint16_t>(24 + x),
            y,
            0x10E000);
    }

    void write_attract_tile_row(int row, const std::string& source)
    {
        const uint16_t y = static_cast<uint16_t>(
            ATTRACT_ROW_BOTTOM_Y + (row * 2));

        for (int x = 0; x < 40; x++)
        {
            const char value =
                x < static_cast<int>(source.size()) ? source[x] : ' ';

            const uint8_t tile =
                value == ' ' ? 0 : attract_tile_char(value);

            video.write_tile8(
                attract_tile_address(static_cast<uint16_t>(x), y) + 1,
                tile);
        }
    }

    void shift_stock_rows_down_two_lines()
    {
        std::array<std::array<uint8_t, 40>, 10> staged{};

        for (int row = 0; row < 10; row++)
        {
            const uint16_t y = static_cast<uint16_t>(7 + row);
            for (int x = 0; x < 40; x++)
            {
                staged[row][x] = video.read_tile8(
                    attract_tile_address(static_cast<uint16_t>(x), y) + 1);
            }
        }

        for (int row = 0; row < 10; row++)
        {
            const uint16_t y = static_cast<uint16_t>(9 + row);
            for (int x = 0; x < 40; x++)
            {
                video.write_tile8(
                    attract_tile_address(static_cast<uint16_t>(x), y) + 1,
                    staged[row][x]);
            }
        }
    }

    enum FieldAlign
    {
        FIELD_LEFT,
        FIELD_CENTER,
        FIELD_RIGHT
    };

    void put_field(std::string& row,
                   int x,
                   int width,
                   const std::string& value,
                   FieldAlign align)
    {
        if (x < 0 || x >= 40 || width <= 0)
            return;

        width = std::min(width, 40 - x);
        std::string text = value;

        if (static_cast<int>(text.size()) > width)
            text.resize(width);

        int offset = 0;
        if (align == FIELD_CENTER)
            offset = (width - static_cast<int>(text.size())) / 2;
        else if (align == FIELD_RIGHT)
            offset = width - static_cast<int>(text.size());

        for (size_t i = 0; i < text.size(); i++)
            row[x + offset + static_cast<int>(i)] = text[i];
    }

    void draw_header_field(int x,
                           int width,
                           const char* text,
                           FieldAlign align = FIELD_CENTER)
    {
        std::string row(40, ' ');
        put_field(row, x, width, text, align);

        int first = 0;
        while (first < 40 && row[first] == ' ')
            first++;

        int last = 39;
        while (last >= first && row[last] == ' ')
            last--;

        if (first <= last)
            draw_attract_text(
                static_cast<uint16_t>(first),
                7,
                row.substr(first, last - first + 1),
                OHud::GREEN);
    }

    int bcd_to_decimal(uint8_t value)
    {
        return ((value >> 4) * 10) + (value & 0x0F);
    }

    void format_counter(uint16_t counter, char* dst, size_t size)
    {
        if (!counter)
        {
            std::snprintf(dst, size, "--'--\"--");
            return;
        }

        uint8_t converted[3] = {0, 0, 0};
        outils::convert_counter_to_time(counter, converted);

        std::snprintf(
            dst,
            size,
            "%02u'%02u\"%02u",
            static_cast<unsigned>(converted[0]),
            static_cast<unsigned>(bcd_to_decimal(converted[1])),
            static_cast<unsigned>(bcd_to_decimal(converted[2])));
    }

    void format_endless_time(uint32_t ticks, char* dst, size_t size)
    {
        if (!ticks)
        {
            std::snprintf(dst, size, "--'--\"--");
            return;
        }

        const uint64_t centiseconds =
            (static_cast<uint64_t>(ticks) * 100ULL) / 60ULL;
        const uint32_t minutes =
            static_cast<uint32_t>(centiseconds / 6000ULL);
        const uint32_t seconds =
            static_cast<uint32_t>((centiseconds / 100ULL) % 60ULL);
        const uint32_t hundredths =
            static_cast<uint32_t>(centiseconds % 100ULL);

        std::snprintf(
            dst,
            size,
            "%02u'%02u\"%02u",
            minutes,
            seconds,
            hundredths);
    }

    char read_initial(xml_parser::ptree& data, const std::string& key)
    {
        const std::string value = data.get_string(key, "_");
        return value.empty() || value[0] == '_' ? ' ' : value[0];
    }

    std::string initials_text(char i1, char i2, char i3)
    {
        std::string result;
        result += i1 == ' ' ? '-' : i1;
        result += i2 == ' ' ? '-' : i2;
        result += i3 == ' ' ? '-' : i3;
        return result;
    }

    void prepare_endless_attract_rows()
    {
        xml_parser::ptree data("endless_scores");
        xml_parser::read_xml(
            config.data.save_path + "hiscores_endless.xml",
            data);

        for (int row_index = 0; row_index < ATTRACT_VISIBLE_ROWS; row_index++)
        {
            const std::string tag =
                "score" + Utils::to_string(row_index);

            const uint16_t stages = static_cast<uint16_t>(
                data.get_int(tag + ".stages", 0));
            const uint32_t distance = static_cast<uint32_t>(
                data.get_int(tag + ".distance_tenths", 0));
            const uint32_t ticks = static_cast<uint32_t>(
                data.get_int(tag + ".time_ticks", 0));

            std::string row(40, ' ');

            if (stages || distance || ticks)
            {
                const std::string initials = initials_text(
                    read_initial(data, tag + ".initial1"),
                    read_initial(data, tag + ".initial2"),
                    read_initial(data, tag + ".initial3"));

                char stages_text[8];
                std::snprintf(
                    stages_text,
                    sizeof(stages_text),
                    "%u",
                    static_cast<unsigned>(stages));

                char distance_text[16];
                std::snprintf(
                    distance_text,
                    sizeof(distance_text),
                    "%u.%u KM",
                    static_cast<unsigned>(distance / 10),
                    static_cast<unsigned>(distance % 10));

                char time_text[16];
                format_endless_time(ticks, time_text, sizeof(time_text));

                put_field(
                    row, 0, 3,
                    Utils::to_string(row_index + 1),
                    FIELD_CENTER);
                put_field(row, 4, 5, initials, FIELD_CENTER);
                put_field(row, 10, 7, stages_text, FIELD_CENTER);
                put_field(row, 18, 11, distance_text, FIELD_CENTER);
                put_field(row, 31, 9, time_text, FIELD_CENTER);
            }

            write_attract_tile_row(row_index, row);
        }
    }

    const char* time_trial_name(int track)
    {
        static const char* NAMES[TimeTrialRecords::TRACK_COUNT] =
        {
            "COCONUT BEACH",
            "GATEWAY",
            "DEVILS CANYON",
            "DESERT",
            "ALPS",
            "CLOUDY MOUNTAIN",
            "WILDERNESS",
            "OLD CAPITAL",
            "WHEAT FIELD",
            "SEASIDE TOWN",
            "VINEYARD",
            "DEATH VALLEY",
            "DESOLATION HILL",
            "AUTOBAHN",
            "LAKESIDE"
        };

        return track >= 0 && track < TimeTrialRecords::TRACK_COUNT ?
            NAMES[track] : "UNKNOWN";
    }

    void prepare_time_trial_attract_rows(int page)
    {
        xml_parser::ptree data("timetrial_scores");
        xml_parser::read_xml(config.data.file_ttrial, data);

        const bool traffic_on = attract_time_trial_with_traffic(page);
        const char* traffic_tag =
            traffic_on ? ".traffic_on.entry0" : ".traffic_off.entry0";

        const int first_track =
            attract_time_trial_page_index(page) * ATTRACT_VISIBLE_ROWS;

        for (int row_index = 0;
             row_index < ATTRACT_VISIBLE_ROWS;
             row_index++)
        {
            const int track = first_track + row_index;
            const std::string track_base =
                "time_trial.track" +
                Utils::to_string(track) +
                traffic_tag;

            uint16_t total = static_cast<uint16_t>(
                data.get_int(track_base + ".total", 0));
            std::string initial_base = track_base;

            if (traffic_on && !total)
            {
                const std::string legacy_base =
                    "time_trial.record" + Utils::to_string(track);
                total = static_cast<uint16_t>(
                    data.get_int(legacy_base + ".total", 0));

                if (total)
                    initial_base = legacy_base;
            }

            const std::string initials = total ?
                initials_text(
                    read_initial(data, initial_base + ".initial1"),
                    read_initial(data, initial_base + ".initial2"),
                    read_initial(data, initial_base + ".initial3")) :
                "---";

            char time_text[16];
            format_counter(total, time_text, sizeof(time_text));

            std::string row(40, ' ');
            put_field(
                row,
                0,
                17,
                time_trial_name(track),
                FIELD_LEFT);
            put_field(row, 19, 5, initials, FIELD_CENTER);
            put_field(row, 27, 13, time_text, FIELD_CENTER);

            write_attract_tile_row(row_index, row);
        }
    }

    const char* attract_page_name(int page)
    {
        switch (page)
        {
            case ATTRACT_SCORE_ORIGINAL:
                return "ORIGINAL";
            case ATTRACT_SCORE_ORIGINAL_JAPAN:
                return "ORIGINAL JAPAN";
            case ATTRACT_SCORE_CONTINUOUS:
                return "CONTINUOUS";
            case ATTRACT_SCORE_ENDLESS:
                return "ENDLESS";
            case ATTRACT_SCORE_TTRIAL_WITH_1:
            case ATTRACT_SCORE_TTRIAL_WITH_2:
            case ATTRACT_SCORE_TTRIAL_WITH_3:
                return "TIME TRIAL WITH TRAFFIC";
            case ATTRACT_SCORE_TTRIAL_WITHOUT_1:
            case ATTRACT_SCORE_TTRIAL_WITHOUT_2:
            case ATTRACT_SCORE_TTRIAL_WITHOUT_3:
                return "TIME TRIAL WITHOUT TRAFFIC";
            default:
                return "";
        }
    }

    void draw_attract_page_header(int page)
    {
        draw_best_outrunners_title();
        draw_attract_centered(4, attract_page_name(page), OHud::GREEN);

        if (page == ATTRACT_SCORE_ORIGINAL ||
            page == ATTRACT_SCORE_ORIGINAL_JAPAN)
        {
            draw_header_field(7, 8, "SCORE");
            draw_header_field(16, 5, "NAME");
            draw_header_field(22, 7, "ROUTE");
            draw_header_field(29, 8, "RECORD");
        }
        else if (page == ATTRACT_SCORE_CONTINUOUS)
        {
            draw_header_field(7, 8, "SCORE");
            draw_header_field(16, 5, "NAME");
            draw_header_field(29, 8, "RECORD");
        }
        else if (page == ATTRACT_SCORE_ENDLESS)
        {
            draw_header_field(0, 3, "#");
            draw_header_field(4, 5, "NAME");
            draw_header_field(10, 7, "STAGES");
            draw_header_field(18, 11, "DISTANCE");
            draw_header_field(31, 9, "TIME");
        }
        else if (attract_time_trial_page(page))
        {
            draw_header_field(0, 17, "COURSE NAME", FIELD_LEFT);
            draw_header_field(19, 5, "NAME");
            draw_header_field(27, 13, "RECORD");
        }
    }

    void restore_attract_runtime()
    {
        config.engine.jap = attract_saved_jap;
        outrun.cannonball_mode = attract_saved_mode;
        attract_score_rotation_active = false;
    }

    void configure_attract_page(OHiScore& score, int page)
    {
        attract_score_page = page;
        score.init_base();

        const int runtime_jap = config.engine.jap;

        switch (page)
        {
            case ATTRACT_SCORE_ORIGINAL:
                config.engine.jap = 0;
                score.init_def_scores();
                config.load_scores(true);
                config.engine.jap = runtime_jap;
                break;
            case ATTRACT_SCORE_ORIGINAL_JAPAN:
                config.engine.jap = 1;
                score.init_def_scores();
                config.load_scores(true);
                config.engine.jap = runtime_jap;
                break;
            case ATTRACT_SCORE_CONTINUOUS:
                config.engine.jap = 0;
                score.init_def_scores();
                config.load_scores(false);
                config.engine.jap = runtime_jap;
                break;
            case ATTRACT_SCORE_ENDLESS:
            case ATTRACT_SCORE_TTRIAL_WITH_1:
            case ATTRACT_SCORE_TTRIAL_WITH_2:
            case ATTRACT_SCORE_TTRIAL_WITH_3:
            case ATTRACT_SCORE_TTRIAL_WITHOUT_1:
            case ATTRACT_SCORE_TTRIAL_WITHOUT_2:
            case ATTRACT_SCORE_TTRIAL_WITHOUT_3:
                break;
        }
    }

    bool attract_timer_will_expire_this_tick()
    {
        if (config.engine.fix_timer)
            return ostats.time_counter == 1 &&
                   ostats.frame_counter <= 1;

        return ostats.time_counter == 0 &&
               ostats.frame_counter <= 0;
    }
}

void OHiScore::init()
{
    if (attract_score_screen())
    {
        attract_saved_jap = config.engine.jap;
        attract_saved_mode = outrun.cannonball_mode;
        attract_score_rotation_active = true;
        configure_attract_page(*this, ATTRACT_SCORE_ORIGINAL);
        return;
    }

    stabilize_score_background();

    if (time_trial_record_screen())
    {
        time_trial_score_audio_started = false;
        time_trial_records.init_screen();
        return;
    }

    if (!endless_gameover_score_screen())
    {
        init_base();
        return;
    }

    endless_score_audio_started = false;
    endless_hiscore.capture_result(
        outrun.endless_stage,
        ostats.score);

    endless_hiscore.init_screen();
}

void OHiScore::display_scores()
{
    if (!attract_score_rotation_active || !attract_score_screen())
    {
        display_scores_base();
        return;
    }

    const bool stock_page =
        attract_score_page == ATTRACT_SCORE_ORIGINAL ||
        attract_score_page == ATTRACT_SCORE_ORIGINAL_JAPAN ||
        attract_score_page == ATTRACT_SCORE_CONTINUOUS;

    switch (best_or_state)
    {
        case 0:
        {
            video.clear_text_ram();

            if (stock_page)
            {
                const int runtime_mode = outrun.cannonball_mode;
                outrun.cannonball_mode =
                    attract_score_page == ATTRACT_SCORE_CONTINUOUS ?
                        Outrun::MODE_CONT :
                        Outrun::MODE_ORIGINAL;

                blit_score_table();
                outrun.cannonball_mode = runtime_mode;

                shift_stock_rows_down_two_lines();
                video.clear_text_ram();
            }
            else
            {
                clear_score_tile_page();

                if (attract_score_page == ATTRACT_SCORE_ENDLESS)
                    prepare_endless_attract_rows();
                else
                    prepare_time_trial_attract_rows(attract_score_page);
            }

            setup_minicars();
            dest_total = 0;
            best_or_state = 1;
            break;
        }

        case 1:
        {
            uint32_t dst = 0x11047C + 0x100;
            uint32_t tiles_adr = TILES_MINICARS1;

            for (int i = 0; i < ATTRACT_VISIBLE_ROWS; i++)
            {
                minicar_entry* minicar = &minicars[i];

                if (!(minicar->dst_reached & BIT_0))
                {
                    if ((minicar->pos >> 8) >= 0x5A)
                    {
                        minicar->dst_reached |= BIT_0;
                        dest_total++;
                    }

                    minicar->speed += minicar->base_speed;

                    if (minicar->speed >= 0x200)
                        minicar->speed = 0x180;

                    minicar->pos += minicar->speed;

                    const int16_t pos =
                        (minicar->pos >> 8) & 0xFFFE;

                    if (stock_page)
                    {
                        setup_minicars_pal(minicar);
                    }
                    else
                    {
                        const int screen_x = 38 - (pos / 2);

                        minicar->tile_props = 0x8400;

                        if (attract_score_page == ATTRACT_SCORE_ENDLESS)
                        {
                            if (screen_x < 3)
                                minicar->tile_props = 0x8600;
                            else if (screen_x >= 4 && screen_x < 9)
                                minicar->tile_props = 0x8200;
                        }
                        else if (attract_time_trial_page(attract_score_page))
                        {
                            if (screen_x >= 19 && screen_x < 24)
                                minicar->tile_props = 0x8200;
                        }
                    }

                    uint32_t textram_adr = dst - pos;
                    uint32_t tiles_smoke_adr = TILES_MINICARS2;

                    if ((minicar->pos >> 8) & BIT_0)
                    {
                        video.write_text32(
                            &textram_adr,
                            roms.rom0.read32(tiles_adr));
                        video.write_text32(
                            &textram_adr,
                            roms.rom0.read32(&tiles_smoke_adr));
                        video.write_text16(
                            &textram_adr,
                            roms.rom0.read16(&tiles_smoke_adr));
                    }
                    else
                    {
                        video.write_text32(
                            &textram_adr,
                            roms.rom0.read32(4 + tiles_adr));
                        video.write_text16(
                            &textram_adr,
                            roms.rom0.read16(8 + tiles_adr));
                        video.write_text32(
                            &textram_adr,
                            roms.rom0.read32(&tiles_smoke_adr));
                        video.write_text16(
                            &textram_adr,
                            roms.rom0.read16(&tiles_smoke_adr));
                    }

                    const uint8_t bottom_tile = video.read_tile8(
                        textram_adr - 0x2000 + 1);

                    video.write_text16(
                        textram_adr,
                        bottom_tile ?
                            (bottom_tile | minicar->tile_props) :
                            0);

                    const uint8_t top_tile = video.read_tile8(
                        textram_adr - 0x2000 - 0x7F);

                    video.write_text16(
                        textram_adr - 0x80,
                        top_tile ?
                            (top_tile | minicar->tile_props) :
                            0);
                }

                dst += 0x100;
                tiles_adr += 0x0A;
            }

            if (dest_total >= ATTRACT_VISIBLE_ROWS)
                best_or_state = 2;

            break;
        }

        case 2:
            break;
    }

    draw_attract_page_header(attract_score_page);

    if (ostats.credits)
    {
        restore_attract_runtime();
        return;
    }

    if (!attract_timer_will_expire_this_tick())
        return;

    if (attract_score_page + 1 < ATTRACT_SCORE_PAGE_COUNT)
    {
        configure_attract_page(
            *this,
            attract_score_page + 1);

        ostats.time_counter = ATTRACT_SCORE_SECONDS;
        ostats.frame_counter = ostats.frame_reset;
    }
    else
    {
        restore_attract_runtime();
    }
}

void OHiScore::tick()
{
    if (time_trial_record_screen())
    {
        if (!time_trial_score_audio_started &&
            outrun.game_state == GS_BEST2)
        {
            osoundint.queue_sound(sound::PCM_WAVE);
            osoundint.queue_sound(sound::MUSIC_LASTWAVE);
            time_trial_score_audio_started = true;
        }

        time_trial_records.tick_screen();
        return;
    }

    if (endless_gameover_score_screen())
    {
        if (!endless_score_audio_started &&
            outrun.game_state == GS_BEST2)
        {
            osoundint.queue_sound(sound::PCM_WAVE);
            osoundint.queue_sound(sound::MUSIC_LASTWAVE);
            endless_score_audio_started = true;
        }

        endless_hiscore.tick_screen();
        return;
    }

    tick_base();
}

int OHiScore::score_position()
{
    if (time_trial_record_screen())
    {
        time_trial_records.finish_flow();

        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        time_trial_score_audio_started = false;
        return -1;
    }

    if (endless_gameover_score_screen())
    {
        endless_hiscore.save_if_needed();

        outrun.endless_mode = false;
        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        endless_score_audio_started = false;
        return -1;
    }

    return score_position_base();
}

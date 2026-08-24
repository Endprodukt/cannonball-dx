    void prepare_endless_attract_rows()
    {
        xml_parser::ptree data("endless_scores");
        xml_parser::read_xml(
            config.data.save_path + "hiscores_endless.xml",
            data);

        for (int row_index = 0;
             row_index < ATTRACT_VISIBLE_ROWS;
             row_index++)
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
                    row,
                    0,
                    3,
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

        reset_time_trial_scroll_state();

        for (int track = 0; track < TimeTrialRecords::TRACK_COUNT; track++)
        {
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

            attract_ttrial_rows[track] = row;
        }

        // Courses 1-5 are initially revealed by the original mini-car routine.
        // Once that animation is complete the smooth overlay takes over at the
        // exact same pixel positions, so the hand-off should be invisible.
        for (int row = 0; row < ATTRACT_VISIBLE_ROWS; row++)
            write_attract_tile_row(row, attract_ttrial_rows[row]);
    }

    void clear_time_trial_text_window()
    {
        for (uint16_t y = ATTRACT_TTRIAL_VIEW_TOP_Y;
             y <= ATTRACT_TTRIAL_VIEW_BOTTOM_Y;
             y++)
        {
            uint32_t dst = ohud.translate(0, y);
            for (int x = 0; x < 40; x++)
                video.write_text16(&dst, 0);
        }
    }

    void setup_time_trial_smooth_overlay()
    {
        clear_time_trial_text_window();
        video.tile_layer->clear_text_scroll_overlay();

        video.tile_layer->configure_text_scroll_overlay(
            ATTRACT_TTRIAL_VIEW_TOP_PIXEL,
            ATTRACT_TTRIAL_VIEW_BOTTOM_PIXEL,
            ATTRACT_TTRIAL_FIRST_ROW_PIXEL,
            ATTRACT_TTRIAL_ROW_PIXELS,
            TimeTrialRecords::TRACK_COUNT);

        for (int track = 0; track < TimeTrialRecords::TRACK_COUNT; track++)
        {
            uint16_t tiles[hwtiles::TEXT_SCROLL_COLUMNS] = {};
            const std::string& row = attract_ttrial_rows[track];

            for (int x = 0; x < hwtiles::TEXT_SCROLL_COLUMNS; x++)
            {
                const char value =
                    x < static_cast<int>(row.size()) ? row[x] : ' ';
                if (value == ' ')
                    continue;

                // Keep the established score-screen palette hierarchy:
                // initials green, course name and record grey/white.
                const uint16_t props =
                    x >= 19 && x < 24 ? 0x8200 : 0x8400;
                tiles[x] = props | attract_tile_char(value);
            }

            video.tile_layer->set_text_scroll_overlay_row(
                track,
                tiles,
                hwtiles::TEXT_SCROLL_COLUMNS);
        }

        video.tile_layer->set_text_scroll_overlay_offset(0);
        attract_ttrial_scroll_fp = 0;
        attract_ttrial_scroll_offset = 0;
        attract_ttrial_hold_frames = 0;
        attract_ttrial_scroll_started = false;
        attract_ttrial_overlay_ready = true;
    }

    bool tick_time_trial_scroll()
    {
        if (!attract_ttrial_overlay_ready)
        {
            setup_time_trial_smooth_overlay();
            return false;
        }

        const int ticks_per_second = std::max(1, config.tick_fps);

        // Let the freshly revealed first five records sit for two seconds.
        if (!attract_ttrial_scroll_started)
        {
            if (++attract_ttrial_hold_frames < ticks_per_second * 2)
                return false;

            attract_ttrial_scroll_started = true;
            attract_ttrial_hold_frames = 0;
            video.tile_layer->set_text_scroll_overlay_motion(
                ATTRACT_TTRIAL_SCROLL_PIXELS_PER_SECOND,
                ATTRACT_TTRIAL_SCROLL_PIXELS);
            return false;
        }

        // The renderer advances the overlay once per DISPLAY frame, not once
        // per game-logic tick. This keeps the movement truly smooth in the
        // common 60 FPS / 30 Hz logic hybrid mode as well as full 60 Hz mode.
        attract_ttrial_scroll_offset =
            video.tile_layer->get_text_scroll_overlay_offset();

        if (attract_ttrial_scroll_offset < ATTRACT_TTRIAL_SCROLL_PIXELS)
            return false;

        // Courses 11-15 are perfectly aligned with the original five-row grid.
        // Leave that final view up for one second before advancing.
        return ++attract_ttrial_hold_frames >= ticks_per_second;
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
            case ATTRACT_SCORE_TTRIAL_WITH:
                return "TIME TRIAL WITH TRAFFIC";
            case ATTRACT_SCORE_TTRIAL_WITHOUT:
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

    uint16_t custom_reveal_tile_props(int page, int reveal_x)
    {
        if (page == ATTRACT_SCORE_ENDLESS)
        {
            if (reveal_x >= 0 && reveal_x < 3)
                return 0x8600;
            if (reveal_x >= 4 && reveal_x < 9)
                return 0x8200;
            return 0x8400;
        }

        if (attract_time_trial_page(page))
        {
            if (reveal_x >= 19 && reveal_x < 24)
                return 0x8200;
            return 0x8400;
        }

        return 0x8400;
    }

    void restore_attract_runtime()
    {
        video.tile_layer->clear_text_scroll_overlay();
        config.engine.jap = attract_saved_jap;

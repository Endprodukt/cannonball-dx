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
            case ATTRACT_SCORE_TTRIAL_WITH:
            case ATTRACT_SCORE_TTRIAL_WITHOUT:
                break;
        }
    }

    bool attract_timer_will_expire_this_tick()
    {
        if (config.engine.fix_timer)
        {
            return ostats.time_counter == 1 &&
                   ostats.frame_counter <= 1;
        }

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

                // ORIGINAL and ORIGINAL JAPAN deliberately use the complete
                // stock table here, including their two-row ROUTE mini-maps and
                // completion-time RECORD field. Continuous suppresses ROUTE in
                // blit_score_table() because MODE_CONT is active temporarily.
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

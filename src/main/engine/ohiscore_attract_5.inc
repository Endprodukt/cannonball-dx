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
                        setup_minicars_pal(minicar);

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

                    if (!stock_page)
                    {
                        const uint16_t row_y = static_cast<uint16_t>(
                            ATTRACT_ROW_BOTTOM_Y + (i * 2));
                        const uint32_t row_start = ohud.translate(0, row_y);

                        int reveal_x = -1;
                        if (textram_adr >= row_start)
                        {
                            reveal_x = static_cast<int>(
                                (textram_adr - row_start) >> 1);
                        }

                        minicar->tile_props = custom_reveal_tile_props(
                            attract_score_page,
                            reveal_x);
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

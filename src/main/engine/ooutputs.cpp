/***************************************************************************
    Process Outputs - CannonBall-SE external output extensions.

    The existing output implementation, including SmartyPi console output,
    is retained verbatim in ooutputs_base.cpp. This wrapper adds MAME-
    compatible network and Windows outputs in parallel.
***************************************************************************/

#include "engine/ooutputs.hpp"
#include "engine/external_outputs.hpp"
#include "engine/external_output_settings.hpp"

// Keep the existing SmartyPi output implementation unchanged, but retain it
// under a private name so the public method can add the external transports.
#define writeDigitalToConsole writeDigitalToConsole_base
#include "engine/ooutputs_base.cpp"
#undef writeDigitalToConsole

#include "main.hpp"
#include "engine/oferrari.hpp"
#include "engine/ohud.hpp"
#include "engine/omusic.hpp"
#include "engine/oroad.hpp"
#include "engine/osprites.hpp"
#include "engine/ostats.hpp"
#include "engine/otraffic.hpp"
#include "sdl2/input.hpp"

namespace
{
    ExternalOutputs external_outputs;

    bool direct_view1_old = false;
    bool direct_view2_old = false;
    bool direct_view3_old = false;
    int music_mode_synced = -1;
    bool music_color_initialized = false;

    void handle_direct_view_buttons(bool controls_active)
    {
        const bool view1 = input.is_pressed(Input::VIEW1);
        const bool view2 = input.is_pressed(Input::VIEW2);
        const bool view3 = input.is_pressed(Input::VIEW3);

        if (controls_active)
        {
            if (view1 && !direct_view1_old)
                oroad.set_view_mode(ORoad::VIEW_ORIGINAL);
            else if (view2 && !direct_view2_old)
                oroad.set_view_mode(ORoad::VIEW_ELEVATED);
            else if (view3 && !direct_view3_old)
                oroad.set_view_mode(ORoad::VIEW_INCAR);
        }

        direct_view1_old = view1;
        direct_view2_old = view2;
        direct_view3_old = view3;
    }

    void sync_music_selection_mode(bool music_selection)
    {
        if (!music_selection)
        {
            music_mode_synced = -1;
            return;
        }

        const int selected = omusic.get_game_mode();

        // A newly selected Time Trial still needs the course map. Keep the
        // music-selection timer alive so the normal timeout cannot start a
        // stale/unselected Time Trial track. A legacy Time Trial has already
        // chosen its course, so its normal timer behaviour is preserved.
        if (selected == Outrun::MODE_TTRIAL &&
            outrun.cannonball_mode != Outrun::MODE_TTRIAL)
        {
            ostats.time_counter = config.sound.music_timer;
            ostats.frame_counter = ostats.frame_reset;
            music_mode_synced = selected;
            return;
        }

        if (selected != Outrun::MODE_ORIGINAL &&
            selected != Outrun::MODE_CONT)
        {
            music_mode_synced = selected;
            return;
        }

        // Keep the underlying engine mode aligned with what the player sees.
        // This also makes the original Music Select auto-timeout launch the
        // selected Original/Continuous mode rather than whichever mode booted.
        outrun.cannonball_mode = selected;

        if (selected != music_mode_synced)
        {
            config.load_scores(selected == Outrun::MODE_ORIGINAL);
            music_mode_synced = selected;
        }
    }

    void sync_continuous_traffic_to_difficulty()
    {
        if (outrun.cannonball_mode != Outrun::MODE_CONT)
            return;

        // Use the exact same stage/difficulty table as original OutRun. The
        // Continuous route visits all 15 stages, but stage_lookup_off / 8 still
        // identifies the original stage group (1..5) for traffic purposes.
        static const uint8_t ORIGINAL_TRAFFIC[] =
        {
            2, 2, 3, 4, 5, // Easy
            3, 4, 5, 6, 7, // Normal
            4, 5, 6, 7, 8, // Hard
            5, 6, 7, 8, 8, // Hardest
        };

        int difficulty = config.engine.dip_traffic;
        if (difficulty < 0)
            difficulty = 0;
        else if (difficulty > 3)
            difficulty = 3;

        int stage_group = oroad.stage_lookup_off / 8;
        if (stage_group < 0)
            stage_group = 0;
        else if (stage_group > 4)
            stage_group = 4;

        const uint8_t traffic =
            ORIGINAL_TRAFFIC[(difficulty * 5) + stage_group];

        // Keep both values synchronized. OTraffic::set_max_traffic() samples
        // custom_traffic at checkpoints; updating max_traffic too ensures the
        // new stage-group value takes effect immediately on that same frame.
        outrun.custom_traffic = traffic;
        otraffic.set_custom_max_traffic(traffic);
    }

    void sync_music_car_color(bool music_selection)
    {
        if (!music_selection)
        {
            music_color_initialized = false;

            // Car-colour selection belongs to the player's next run only.
            // Attract/demo mode must always use the canonical red Ferrari,
            // regardless of the colour chosen for the previous game.
            if (cannonball::state == cannonball::STATE_GAME &&
                (outrun.game_state == GS_INIT ||
                 outrun.game_state == GS_ATTRACT))
            {
                config.engine.car_pal = 0;
                oferrari.ferrari_pal = OFerrari::PAL_RED;
            }

            return;
        }

        if (!music_color_initialized)
        {
            // Every new arcade Music Select starts from the canonical red
            // Ferrari. Shifter changes are deliberately per-run choices.
            config.engine.car_pal = 0;
            music_color_initialized = true;
        }
    }

    void draw_music_color_preview(bool music_selection)
    {
        if (!music_selection || outrun.game_state != GS_MUSIC)
            return;

        // OMusic used an experimental centre-screen COLOR row while this
        // feature was being prototyped. Clear both affected rows so the final
        // presentation leaves the centre of the original Music Select clean.
        for (int x = 0; x < 40; x++)
        {
            video.write_text16(ohud.translate(x, 14), 0);
            video.write_text16(ohud.translate(x, 15), 0);
        }

        // Put the instruction on exactly the same row and in exactly the same
        // single-row font/palette as the original FREE PLAY text. Right-align
        // it so FREE PLAY remains untouched on the left side of the screen.
        uint32_t freeplay_record = TEXT1_FREEPLAY;
        const uint32_t freeplay_dst = roms.rom0.read32(&freeplay_record);
        roms.rom0.read16(&freeplay_record); // tile count
        const uint16_t freeplay_data = roms.rom0.read16(&freeplay_record);
        const uint16_t freeplay_pal = (freeplay_data >> 8) & 0xFF;

        const uint32_t freeplay_relative =
            (freeplay_dst - 0x110030) & 0x0FFF;
        const uint16_t freeplay_y =
            static_cast<uint16_t>(freeplay_relative / 0x80);

        const char* text = "CHANGE CAR COLOR WITH GEAR";
        const int length = 26;
        const int x_start = 40 - length;

        // Only clear the right-hand portion. The original FREE PLAY text is
        // redrawn by OHud on the left each frame and must remain untouched.
        for (int x = x_start; x < 40; x++)
            video.write_text16(ohud.translate(x, freeplay_y), 0);

        ohud.blit_text_new(
            static_cast<uint16_t>(x_start),
            freeplay_y,
            text,
            freeplay_pal);

        // Reuse the small Ferrari that normally drives across the course map.
        // The map entry supplies its native size/anchor properties, while the
        // palette is deliberately replaced by the same five Ferrari palettes
        // used by the full-size in-game car.
        const int preview_index = (OSprites::SPRITE_ENTRIES - 0x10) + 5;
        oentry* preview = &osprites.jump_table[preview_index];
        preview->init(preview_index);

        const uint32_t map_ferrari_entry =
            outrun.adr.sprite_coursemap + (25 * 20);

        preview->draw_props = roms.rom0p->read8(map_ferrari_entry + 1);
        preview->shadow = roms.rom0p->read8(map_ferrari_entry + 2);
        preview->zoom = roms.rom0p->read8(map_ferrari_entry + 3);

        // Mirror the FREE PLAY placement: preview the selected car just above
        // the new right-hand instruction rather than in the middle of screen.
        preview->x = 112;
        preview->y = static_cast<int16_t>((freeplay_y * 8) - 12);
        preview->priority = 0x1FF;
        preview->road_priority = 0x1FF;
        preview->addr = outrun.adr.sprite_minicar_right;

        static const uint16_t CAR_PALETTES[] =
        {
            OFerrari::PAL_RED,
            OFerrari::PAL_BLUE,
            OFerrari::PAL_YELLOW,
            OFerrari::PAL_GREEN,
            OFerrari::PAL_CYAN,
        };

        int color = config.engine.car_pal;
        if (color < 0 || color >= 5)
            color = 0;

        preview->pal_src = CAR_PALETTES[color];
        osprites.map_palette(preview);

        // writeDigitalToConsole() runs after the current engine frame has been
        // assembled. Queue the preview here and it joins the Music Select sprite
        // list on the following frame; doing this every frame keeps it stable.
        osprites.do_spr_order_shadows(preview);
    }
}

void OOutputs::writeDigitalToConsole()
{
    const bool music_selection =
        cannonball::state == cannonball::STATE_GAME &&
        (outrun.game_state == GS_INIT_MUSIC ||
         outrun.game_state == GS_MUSIC);

    // Keep the visible Music Select choice and the underlying mode in sync,
    // including the original automatic Music Select timeout behaviour.
    sync_music_selection_mode(music_selection);

    // Keep Continuous traffic tied to the normal OutRun difficulty setting.
    // This is intentionally independent of whether external outputs are enabled;
    // this method is already called every engine tick by main.cpp.
    sync_continuous_traffic_to_difficulty();

    // New runs always begin from the red Ferrari, then allow a temporary colour
    // choice with the shifter for that run. Attract mode is also forced back to
    // red here so a previous player colour never leaks into the demo sequence.
    sync_music_car_color(music_selection);

    // Draw the car-colour instruction and queue the small map Ferrari preview.
    draw_music_color_preview(music_selection);

    // Preserve the original SmartyPi console output path exactly as before.
    writeDigitalToConsole_base();

    const bool view_controls_active =
        outrun.game_state >= GS_START1 &&
        outrun.game_state <= GS_INGAME;

    // During the race the single VIEW lamp remains the normal availability
    // lamp. During music selection it becomes a mode-selection lamp and blinks
    // in sync with the one matching VIEW1/2/3 lamp below.
    const bool view_lamp_active =
        outrun.game_state >= GS_START1 &&
        outrun.game_state < GS_INIT_GAMEOVER;

    handle_direct_view_buttons(view_controls_active);

    const uint8_t view = oroad.get_view_mode();

    // MAMEHooker START lamp behaviour is deliberately cabinet-oriented rather
    // than tied to the original D_START_LAMP bit. In particular, CannonBall's
    // freeplay PRESS START text does not set the original hardware bit.
    const bool press_start_screen =
        outrun.game_state == GS_ATTRACT ||
        outrun.game_state == GS_BEST1 ||
        outrun.game_state == GS_LOGO ||
        outrun.game_state == GS_BEST2;

    const bool press_start_available =
        config.engine.freeplay || ostats.credits > 0;

    // Blink during attract PRESS START and throughout music selection. The
    // attract phase uses the same BIT_4 timing as OHud::draw_insert_coin().
    const bool start_lamp_blink =
        ((press_start_screen && press_start_available) ||
         music_selection) &&
        (outrun.tick_counter & BIT_4);

    // As soon as music selection hands off to the game, keep START steadily
    // illuminated through the driving sequence, race and bonus sequence.
    const bool start_lamp_ingame =
        outrun.game_state >= GS_INIT_GAME &&
        outrun.game_state <= GS_BONUS;

    // Music-select game mode indication. The traditional single VIEW lamp
    // always blinks here, while only the lamp for the selected direct-view
    // button blinks with it. The other two remain off.
    const bool mode_lamp_blink =
        music_selection &&
        (outrun.tick_counter & BIT_4);

    const int selected_game_mode = omusic.get_game_mode();

    const auto& settings = external_output_settings();

    external_outputs.update(
        settings.network,
        settings.windows,
        settings.port,
        cannonball::state != cannonball::STATE_QUIT,
        (start_lamp_blink || start_lamp_ingame) ? 1 : 0,
        is_set(D_BRAKE_LAMP),
        music_selection
            ? (mode_lamp_blink ? 1 : 0)
            : (view_lamp_active ? 1 : 0),
        music_selection
            ? ((mode_lamp_blink && selected_game_mode == Outrun::MODE_ORIGINAL) ? 1 : 0)
            : ((view_lamp_active && view == ORoad::VIEW_ORIGINAL) ? 1 : 0),
        music_selection
            ? ((mode_lamp_blink && selected_game_mode == Outrun::MODE_CONT) ? 1 : 0)
            : ((view_lamp_active && view == ORoad::VIEW_ELEVATED) ? 1 : 0),
        music_selection
            ? ((mode_lamp_blink && selected_game_mode == Outrun::MODE_TTRIAL) ? 1 : 0)
            : ((view_lamp_active && view == ORoad::VIEW_INCAR) ? 1 : 0));
}

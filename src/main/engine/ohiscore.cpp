/***************************************************************************
    Best Outrunners - CannonBall DX score-screen wrapper.

    The original score implementation is preserved in ohiscore_base.cpp.
    Endless and Time Trial substitute their dedicated tables only at game end;
    attract-mode, Original and Continuous score handling continue to use the
    preserved implementation.
***************************************************************************/

// Pre-include the preserved implementation's dependencies before temporarily
// renaming OHiScore methods. This keeps generic tokens such as init/tick away
// from unrelated declarations in those headers.
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
#include <iostream>

#include "engine/endless_hiscore.hpp"
#include "engine/time_trial_records.hpp"

#define init init_base
#define tick tick_base
#define score_position score_position_base
#include "ohiscore_base.cpp"
#undef score_position
#undef tick
#undef init

EndlessHiScore endless_hiscore;
TimeTrialRecords time_trial_records;

namespace
{
    bool endless_score_audio_started = false;
    bool time_trial_score_audio_started = false;

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

    void stabilize_score_background()
    {
        // Dedicated game-end score screens can be entered from any stage, and
        // Time Trial in particular normally leaves the selected course's tilemap
        // and palette live. Rebuild the complete common Best OutRunners scene
        // for those dedicated screens only.
        //
        // IMPORTANT: never call this for GS_INIT_BEST1/GS_BEST1. The original
        // attract-mode high-score sequence is an organic overlay on the demo
        // scene that was already running; resetting road/tile/palette state here
        // makes the road disappear and visibly jumps to a different background.
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

        // These are the original dedicated Best OutRunners palette overrides:
        // shaded red/sunset backdrop plus the black score-screen road.
        ohiscore.setup_pal_best();
        ohiscore.setup_road_best();

        oroad.set_view_mode(ORoad::VIEW_ORIGINAL, true);
        oroad.horizon_base = 0x154;
        oroad.horizon_set = 1;
        oroad.road_pos = 0;
        oroad.road_pos_change = 0;
        oroad.tilemap_h_target = 0;
    }
}

void OHiScore::init()
{
    // Preserve the original attract transition exactly: BEST1 must inherit the
    // running demo's road, scenery and palette instead of rebuilding a separate
    // Best OutRunners background. This deliberately leaves every other attract
    // subsystem untouched.
    if (attract_score_screen())
    {
        init_base();
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

void OHiScore::tick()
{
    if (time_trial_record_screen())
    {
        // GS_INIT_BEST2 resets FM/WAV immediately after init(). Start the
        // familiar Last Wave high-score ambience on the first real BEST2 tick.
        if (!time_trial_score_audio_started && outrun.game_state == GS_BEST2)
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
        // GS_INIT_BEST2 queues FM_RESET and clears WAV playback immediately
        // after init(). Start Last Wave on the first real BEST2 tick instead,
        // after that stock reset has completed, so the Endless score screen
        // gets the intended OutRun high-score music and sea ambience.
        if (!endless_score_audio_started && outrun.game_state == GS_BEST2)
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

        // GS_BEST2 performs the normal full engine reset immediately after
        // this call. Switch back to Original first so it initializes Stage 1
        // and returns to attract mode instead of reloading the Time Trial track.
        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        time_trial_score_audio_started = false;
        return -1;
    }

    // The preserved GS_BEST2 exit saves Original/Continuous tables when this
    // reports a valid entry. Endless persists its own XML table, so suppress
    // that legacy save and keep hiscores_continuous.xml untouched.
    if (endless_gameover_score_screen())
    {
        endless_hiscore.save_if_needed();

        // GS_BEST2 immediately performs the full engine reset after this call.
        // Return the runtime to the normal Original attract state first so an
        // Endless MODE_CONT flag cannot leak into the demo/audio sequence.
        outrun.endless_mode = false;
        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        endless_score_audio_started = false;
        return -1;
    }

    return score_position_base();
}

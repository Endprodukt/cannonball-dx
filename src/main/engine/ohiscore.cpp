/***************************************************************************
    Best Outrunners - CannonBall DX Endless wrapper.

    The original score implementation is preserved in ohiscore_base.cpp.
    Endless substitutes only the game-end init/tick calls; attract-mode score
    display and Original/Continuous score handling continue to use the exact
    preserved implementation, with a shared stable high-score background.
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
#include <iostream>

#include "engine/endless_hiscore.hpp"

#define init init_base
#define tick tick_base
#define score_position score_position_base
#include "ohiscore_base.cpp"
#undef score_position
#undef tick
#undef init

EndlessHiScore endless_hiscore;

namespace
{
    bool endless_score_audio_started = false;

    bool endless_gameover_score_screen()
    {
        return outrun.endless_mode &&
               outrun.cannonball_mode == Outrun::MODE_CONT &&
               (outrun.game_state == GS_INIT_BEST2 ||
                outrun.game_state == GS_BEST2);
    }

    void stabilize_score_background()
    {
        // High-score screens can be entered after many different road profiles
        // (and Endless can finish on any of the fifteen stages). Reusing the
        // live road state makes the sunset/horizon sit at different heights.
        // Use the same flat Stage 1 road baseline and stock Best OutRunners
        // horizon for every high-score presentation: attract, Original,
        // Continuous and Endless.
        oroad.init();
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
    // Keep the attractive, correctly positioned Best OutRunners sunset
    // consistent on every score screen instead of inheriting the previous
    // stage's road/horizon state.
    stabilize_score_background();

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

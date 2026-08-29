/***************************************************************************
    Ferrari palette extensions for CannonBall-SE.

    The original Ferrari implementation is kept verbatim in oferrari_base.cpp.
    This wrapper extends its palette list and handles live F10 colour cycling
    during normal Ferrari updates. Attract mode shares the same edge state via
    car_palette_hotkey.hpp so the key can also be handled there safely.
***************************************************************************/

// Keep the multiplayer include first on Windows so Winsock2 is loaded before
// any platform headers that may be reached through SDL/config includes.
#include "engine/multiplayer.hpp"
#include "engine/car_palette_hotkey.hpp"
#include "engine/car_palette_state.hpp"

// Pre-include the original implementation's dependencies so the temporary
// macros below only affect tokens in oferrari_base.cpp itself.
#include "engine/oanimseq.hpp"
#include "engine/oattractai.hpp"
#include "engine/obonus.hpp"
#include "engine/ocrash.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/ooutputs.hpp"
#include "engine/ostats.hpp"
#include "engine/outils.hpp"
#include "engine/oferrari.hpp"

// Extend the existing five-colour initializer without modifying the preserved
// base implementation. PAL_CYAN occurs there only in FERRARI_PALETTES[].
#define PAL_CYAN PAL_CYAN, OFerrari::PAL_BLACK, OFerrari::PAL_WHITE, OFerrari::PAL_SILVER

// Keep the original tick logic as tick_base(); the wrapper below adds only the
// live colour hotkey and then delegates to the unchanged implementation.
#define tick tick_base
#include "oferrari_base.cpp"
#undef tick
#undef PAL_CYAN

namespace
{
    bool ttrial_goal_randomized = false;

    constexpr uint8_t MULTIPLAYER_GRID_SPRITE = OSprites::SPRITE_FLAG + 1;
    constexpr int16_t MULTIPLAYER_GRID_SEPARATION = 0x48;

    int multiplayer_grid_source_stable_frames = 0;

    int16_t multiplayer_peer_screen_x()
    {
        const oentry& local = osprites.jump_table[OSprites::SPRITE_FERRARI];
        const int direction = multiplayer.player_number() == 2 ? -1 : 1;
        return static_cast<int16_t>(
            static_cast<int>(local.x) + direction * MULTIPLAYER_GRID_SEPARATION);
    }

    void disable_multiplayer_peer_slot()
    {
        oentry* peer = &osprites.jump_table[MULTIPLAYER_GRID_SPRITE];
        peer->control &= ~OSprites::ENABLE;
        peer->hidden = 1;
    }

    // START1/2/3 deliberately do not use the network road projection. The local
    // Ferrari has already been completely drawn by the preserved code on the
    // render half-frame, so copy that finished sprite into a spare jump-table
    // entry and move only the copy sideways on screen. This gives us the same
    // two-car grid we had before without touching car_x_pos, camera position,
    // road arrays or the joiner's fragile race initialization.
    void draw_multiplayer_grid_peer()
    {
        if (!multiplayer.grid_start_active() ||
            !multiplayer.connected() ||
            outrun.game_state < GS_START1 ||
            outrun.game_state > GS_START3)
        {
            multiplayer_grid_source_stable_frames = 0;
            return;
        }

        // Match the preserved Ferrari renderer exactly: logic runs on the tick
        // half-frame and sprite submission runs only on the render half-frame.
        // Submitting the same jump-table entry on both halves causes duplicate
        // sprite-order entries and visible ghost/trail copies.
        if (outrun.tick_frame)
            return;

        const oentry& local = osprites.jump_table[OSprites::SPRITE_FERRARI];
        if (!(local.control & OSprites::ENABLE) ||
            local.addr == 0 ||
            local.zoom == 0 ||
            local.y != 221)
        {
            multiplayer_grid_source_stable_frames = 0;
            return;
        }

        // The first Ferrari frame immediately after FERRARI_SEQ -> FERRARI_LOGIC
        // can still contain transitional sprite data. Wait for a second valid
        // render frame instead of briefly drawing that transient frame and
        // producing the flicker seen just before the car settles on the grid.
        if (++multiplayer_grid_source_stable_frames < 2)
            return;

        oentry* peer = &osprites.jump_table[MULTIPLAYER_GRID_SPRITE];
        *peer = local;
        peer->jump_index = MULTIPLAYER_GRID_SPRITE;
        peer->x = multiplayer_peer_screen_x();
        peer->hidden = 0;

        // Re-map the copied palette entry in case this slot previously belonged
        // to another temporary object. For the countdown the copy intentionally
        // uses the local Ferrari palette; the real network renderer takes over
        // at GO and uses the transmitted peer palette again.
        osprites.map_palette(peer);
        osprites.do_spr_order_shadows(peer);
    }

    // At GO the full network renderer takes over. When both cars are still very
    // close longitudinally its near-camera perspective is hypersensitive to tiny
    // road/camera differences. Keep only the final screen X close to the stable
    // grid position until the peer has moved farther into perspective. Physics
    // and transmitted world coordinates remain completely untouched.
    void stabilize_near_peer_projection()
    {
        oentry* peer = &osprites.jump_table[MULTIPLAYER_GRID_SPRITE];
        if (!(peer->control & OSprites::ENABLE))
            return;

        if (peer->road_priority > 0x1C0)
            peer->x = multiplayer_peer_screen_x();
    }
}

void OFerrari::cycle_car_palette()
{
    const int size = sizeof(FERRARI_PALETTES) / sizeof(FERRARI_PALETTES[0]);

    config.engine.car_pal++;
    if (config.engine.car_pal >= size)
        config.engine.car_pal = 0;

    ferrari_pal = FERRARI_PALETTES[config.engine.car_pal];
}

void OFerrari::tick()
{
    // Player 2 deliberately remains in the Attract engine while waiting for
    // Player 1 to finish the shared race setup. During that lobby state the
    // joiner owns a temporary per-race colour, so do not restore the saved
    // attract/default colour every frame.
    const bool multiplayer_lobby_colour = multiplayer.keep_lobby_color();

    if (!multiplayer_lobby_colour &&
        outrun.game_state >= GS_INIT && outrun.game_state <= GS_LOGO)
    {
        const int color =
            car_palette_state::get_default(config.engine.car_pal);
        config.engine.car_pal = color;
        ferrari_pal = car_palette_state::palette_source(color);
    }

    if (car_palette_hotkey::pressed())
    {
        cycle_car_palette();

        // F10 is also useful on Player 2's waiting screen, but that temporary
        // choice belongs only to this race. Never turn it into the persistent
        // Attract default while the multiplayer lobby owns the colour.
        if (!multiplayer_lobby_colour && outrun.game_state == GS_ATTRACT)
        {
            car_palette_state::set_default(config.engine.car_pal);
            config.save();
        }
    }

    // A normal OutRun ending ties one of five end animations to the route.
    // Time Trial repeatedly finishes the same selected course, so choose one
    // of those five animation sets at random once when its GOAL sequence starts.
    // The bonus road is already loaded at this point; only the visible character/
    // celebration animation changes, which keeps the Time Trial course intact.
    const bool ttrial_goal =
        outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
        (outrun.game_state == GS_INIT_BONUS || outrun.game_state == GS_BONUS);

    if (ttrial_goal && !ttrial_goal_randomized)
    {
        oanimseq.end_seq = static_cast<uint8_t>(outils::random() % 5);
        ttrial_goal_randomized = true;
    }
    else if (!ttrial_goal)
    {
        ttrial_goal_randomized = false;
    }

    // Two-player races use a dedicated grid start. GS_INIT_GAME has already
    // rebuilt the normal Ferrari/passenger sprite pointers at this point, so
    // switch straight from the intro state to the ordinary in-game Ferrari
    // state before the preserved tick sees START1.
    multiplayer.prepare_grid_ferrari();

    // Keep the known-stable audio behaviour. Music is now confirmed synchronized,
    // so do not touch the audio path while fixing only the Ferrari rendering.
    if (multiplayer.player_number() == 1)
        multiplayer.start_grid_music_once();

    // The original START1 counter contains an extra 50 ticks for the Ferrari
    // drive-in. There is no drive-in in multiplayer, so remove that dead time.
    // START1/2/3 then become three equal countdown phases before GO.
    if (multiplayer.grid_start_active() &&
        outrun.game_state == GS_START1 &&
        ostats.frame_counter > ostats.frame_reset)
    {
        ostats.frame_counter = ostats.frame_reset;
    }

    tick_base();

    multiplayer.draw_lobby_overlay();

    if (outrun.game_state >= GS_START1 && outrun.game_state <= GS_START3)
    {
        draw_multiplayer_grid_peer();
    }
    else if (outrun.game_state == GS_INGAME)
    {
        multiplayer_grid_source_stable_frames = 0;

        if (outrun.tick_frame)
        {
            // Explicit handoff frame: remove the static countdown copy. The
            // real network Ferrari will be created on the following render
            // half-frame, so both representations can never be ordered together.
            disable_multiplayer_peer_slot();
        }
        else
        {
            // The preserved Ferrari renderer submits sprites only on this half.
            // Do the same for the network Ferrari. Previously it was submitted
            // on both halves, which accumulated several copies of the same jump
            // entry at slightly different positions (the visible Ferrari train).
            multiplayer.draw_remote_ferrari();
            stabilize_near_peer_projection();
        }
    }
    else
    {
        multiplayer_grid_source_stable_frames = 0;
    }
}

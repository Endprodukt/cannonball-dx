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

    // The final two entries in OSprites::jump_table are unused by the original
    // game. The network Ferrari already uses +22, so use that same dedicated
    // peer slot for the static countdown representation. At GO it is simply
    // overwritten by the live network representation; there is never a second
    // peer body slot to hand off or leave behind in the alternate sprite buffer.
    constexpr uint8_t MULTIPLAYER_PEER_SPRITE = OSprites::SPRITE_ENTRIES + 22;
    constexpr int16_t MULTIPLAYER_GRID_SEPARATION = 0x48;

    int16_t multiplayer_grid_screen_x()
    {
        return multiplayer.player_number() == 2
            ? -MULTIPLAYER_GRID_SEPARATION
            : MULTIPLAYER_GRID_SEPARATION;
    }

    // CannonBall submits the normal Ferrari on BOTH tick_frame phases: the
    // logic phase ends in setup_ferrari_sprite()->draw_sprite(), and the other
    // phase calls draw_sprite() directly. The sprite RAM is double-buffered, so
    // the multiplayer peer must follow the same rule. Submitting it on only one
    // phase leaves one hardware buffer with stale/no peer data, which is exactly
    // the alternating flicker and old stacked position seen during testing.
    void draw_multiplayer_grid_peer()
    {
        if (!multiplayer.grid_start_active() ||
            !multiplayer.connected() ||
            outrun.game_state < GS_START1 ||
            outrun.game_state > GS_START3)
        {
            return;
        }

        oentry* peer = &osprites.jump_table[MULTIPLAYER_PEER_SPRITE];
        peer->init(MULTIPLAYER_PEER_SPRITE);
        peer->control = OSprites::ENABLE;
        peer->draw_props = oentry::BOTTOM;
        peer->shadow = 3;
        peer->priority = 0x1FC;
        peer->road_priority = 0x1FC;
        peer->x = multiplayer_grid_screen_x();
        peer->y = 221;
        peer->zoom = 0x7F;
        peer->addr = roms.rom0p->read32(outrun.adr.sprite_ferrari_frames);
        peer->pal_src = oferrari.ferrari_pal;
        peer->hidden = 0;

        osprites.map_palette(peer);
        osprites.do_spr_order_shadows(peer);
    }

    void draw_multiplayer_live_peer()
    {
        // The current network renderer still requests OSprites' generic object
        // shadow. A Ferrari has its own dedicated shadow implementation, so the
        // generic shadow can resemble a second displaced body in perspective.
        // Keep exactly the ordered body for now; a proper peer Ferrari shadow can
        // be added later as its own dedicated sprite.
        const uint16_t shadows_before = osprites.spr_cnt_shadow;
        multiplayer.draw_remote_ferrari();
        if (osprites.spr_cnt_shadow > shadows_before)
            osprites.spr_cnt_shadow = shadows_before;
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

    // Both machines deliberately restart the selected track at the first shared
    // START1 frame. Player 1 may have been previewing it for several seconds and
    // Player 2 may only just have loaded it; FM_RESET + play_music() on BOTH sides
    // makes the synchronized grid the single audible time origin.
    multiplayer.start_grid_music_once();

    // The original START1 counter contains an extra 50 ticks for the Ferrari
    // drive-in. There is no drive-in in multiplayer, so remove that dead time.
    if (multiplayer.grid_start_active() &&
        outrun.game_state == GS_START1 &&
        ostats.frame_counter > ostats.frame_reset)
    {
        ostats.frame_counter = ostats.frame_reset;
    }

    tick_base();

    // Player 2 stays in Attract while waiting. Clear the text layer AFTER the
    // normal Attract code ran, then redraw only the multiplayer waiting UI. This
    // guarantees the old JOIN TIME text disappears immediately after joining.
    if (multiplayer_lobby_colour)
        video.clear_text_ram();

    multiplayer.draw_lobby_overlay();

    if (outrun.game_state >= GS_START1 && outrun.game_state <= GS_START3)
    {
        draw_multiplayer_grid_peer();
    }
    else if (outrun.game_state == GS_INGAME)
    {
        // Match the preserved Ferrari exactly and submit on both buffer phases.
        // Restricting the peer to !tick_frame was the source of the alternating
        // flicker/stale duplicate seen in the two sprite buffers.
        draw_multiplayer_live_peer();
    }
}

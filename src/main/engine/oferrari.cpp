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
    // game. Keep the countdown peer in one dedicated slot instead of repeatedly
    // copying/re-purposing animation state from the local Ferrari.
    constexpr uint8_t MULTIPLAYER_GRID_SPRITE = OSprites::SPRITE_ENTRIES + 22;
    constexpr int16_t MULTIPLAYER_GRID_SEPARATION = 0x48;

    bool join_left_down = false;
    bool join_right_down = false;
    bool join_gear1_down = false;
    bool join_gear2_down = false;

    int normalize_join_colour(int colour)
    {
        while (colour < 0) colour += 8;
        while (colour >= 8) colour -= 8;
        return colour;
    }

    void reset_join_colour_input()
    {
        join_left_down = false;
        join_right_down = false;
        join_gear1_down = false;
        join_gear2_down = false;
    }

    void handle_join_colour_input(bool active)
    {
        if (!active)
        {
            reset_join_colour_input();
            return;
        }

        const bool left_now = input.is_pressed(Input::LEFT);
        const bool right_now = input.is_pressed(Input::RIGHT);
        const bool gear1_now = input.is_pressed(Input::GEAR1);
        const bool gear2_now = input.is_pressed(Input::GEAR2);

        int direction = 0;

        // Use our own edge latches here. Multiplayer networking runs at the
        // beginning of OInputs::tick, so relying on has_pressed() there can miss
        // the user's LEFT/RIGHT edge depending on the input backend/order.
        if (left_now && !join_left_down)
            direction = -1;
        else if (right_now && !join_right_down)
            direction = 1;
        else if (gear1_now && !join_gear1_down)
            direction = -1;
        else if (gear2_now && !join_gear2_down)
            direction = 1;

        join_left_down = left_now;
        join_right_down = right_now;
        join_gear1_down = gear1_now;
        join_gear2_down = gear2_now;

        if (direction == 0)
            return;

        const int colour = normalize_join_colour(config.engine.car_pal + direction);
        config.engine.car_pal = colour;
        ferrari_pal = FERRARI_PALETTES[colour];

        // Do not let the colour-select buttons steer the Attract car underneath
        // the waiting overlay after they have been consumed here.
        input.keys[Input::LEFT] = false;
        input.keys[Input::RIGHT] = false;

        osoundint.queue_sound(sound::BEEP1);
        std::cout << "[Multiplayer] Player 2 colour -> " << colour << std::endl;
    }

    int16_t multiplayer_grid_screen_x()
    {
        return multiplayer.player_number() == 2
            ? -MULTIPLAYER_GRID_SEPARATION
            : MULTIPLAYER_GRID_SEPARATION;
    }

    // Countdown renderer: fixed, known-good Ferrari frame at a fixed screen
    // position. It deliberately does not read road perspective, car_x_pos or a
    // transitional local Ferrari sprite. This removes the pre-grid flicker.
    void draw_multiplayer_grid_peer()
    {
        if (!multiplayer.grid_start_active() ||
            !multiplayer.connected() ||
            outrun.tick_frame ||
            outrun.game_state < GS_START1 ||
            outrun.game_state > GS_START3)
        {
            return;
        }

        oentry* peer = &osprites.jump_table[MULTIPLAYER_GRID_SPRITE];
        peer->init(MULTIPLAYER_GRID_SPRITE);
        peer->control = OSprites::ENABLE;
        peer->draw_props = oentry::BOTTOM;
        peer->shadow = 3;
        peer->priority = 0x1FC;
        peer->road_priority = 0x1FC;
        peer->x = multiplayer_grid_screen_x();
        peer->y = 221;
        peer->zoom = 0x7F;
        peer->addr = roms.rom0p->read32(outrun.adr.sprite_ferrari_frames);
        peer->pal_src = ferrari_pal;
        peer->hidden = 0;

        osprites.map_palette(peer);
        osprites.do_spr_order_shadows(peer);
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

    // P2 colour selection has its own edge handling so keyboard, D-pad and gear
    // buttons work regardless of the networking/input tick ordering.
    handle_join_colour_input(multiplayer_lobby_colour);

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

    // Keep the known-good synchronized music path untouched.
    if (multiplayer.player_number() == 1)
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
    else if (outrun.game_state == GS_INGAME && !outrun.tick_frame)
    {
        // Gameplay goes back to the original single network-Ferrari path. No
        // extra handoff copy, no near-grid clone and no car_x_pos manipulation.
        multiplayer.draw_remote_ferrari();
    }
}

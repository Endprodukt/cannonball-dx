/***************************************************************************
    Ferrari palette extensions for CannonBall-SE.

    The original Ferrari implementation is kept verbatim in oferrari_base.cpp.
    This wrapper extends its palette list and handles live F10 colour cycling
    during normal Ferrari updates. Attract mode shares the same edge state via
    car_palette_hotkey.hpp so the key can also be handled there safely.
***************************************************************************/

#include "../trackloader.hpp"
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
    struct GoalPiece
    {
        bool valid = false;
        uint32_t addr = 0;
        uint16_t palette = 0;
    };

    GoalPiece ttrial_goal_bottom;
    GoalPiece ttrial_goal_top;
    bool ttrial_goal_checked = false;
    bool ttrial_outro_active = false;

    bool ttrial_run_complete()
    {
        return outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
               outrun.ttrial.laps != 0 &&
               outrun.ttrial.current_lap >= outrun.ttrial.laps;
    }

    bool ttrial_final_approach()
    {
        return outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
               outrun.ttrial.laps != 0 &&
               outrun.game_state == GS_INGAME &&
               static_cast<int>(outrun.ttrial.current_lap) + 1 >=
                   static_cast<int>(outrun.ttrial.laps) &&
               (oroad.road_pos >> 16) > (ROAD_END - 0x180);
    }

    bool find_goal_piece(int wanted_routine, GoalPiece& result)
    {
        // End roads contain the original GOAL/checkpoint artwork. We only need
        // its graphics and palette: the live Time Trial sign keeps its existing
        // world position and perspective handling.
        for (int ending = 0; ending < 5; ++ending)
        {
            const uint32_t end_section = roms.rom0p->read32(
                outrun.adr.road_seg_end + (ending << 2));
            const uint32_t scenery_addr = roms.rom0p->read32(end_section + 8);

            if (scenery_addr >= roms.rom0p->length)
                continue;

            const uint8_t* scenery = &roms.rom0p->rom[scenery_addr];

            for (int record = 0; record < 256; ++record)
            {
                const uint32_t off = static_cast<uint32_t>(record) * 4;
                const uint16_t pos = static_cast<uint16_t>(
                    (scenery[off] << 8) | scenery[off + 1]);

                if (pos == 0xFFFF)
                    break;

                const uint8_t pattern_index = scenery[off + 3];
                uint32_t pattern = trackloader.read_scenerymap_table(pattern_index);

                // Pattern header: frequency, then the final eight-byte entry
                // offset. Scan the pattern for the stock checkpoint/goal
                // routines (5 = lower sign, 6 = upper sign).
                trackloader.read16(trackloader.scenerymap_data, &pattern);
                const int16_t reload = trackloader.read16(
                    trackloader.scenerymap_data, &pattern);

                if (reload < 0 || reload > 0x200 || (reload & 7))
                    continue;

                for (int entry_off = 0; entry_off <= reload; entry_off += 8)
                {
                    const uint32_t entry = pattern + entry_off;
                    const uint8_t props = trackloader.scenerymap_data[entry];
                    const int routine = (props >> 4) & 0x0F;

                    if (routine != wanted_routine)
                        continue;

                    const uint16_t type = static_cast<uint16_t>(
                        trackloader.scenerymap_data[entry + 5] << 2);
                    result.addr = roms.rom0p->read32(
                        outrun.adr.sprite_type_table + type);
                    result.palette = trackloader.scenerymap_data[entry + 7];
                    result.valid = true;
                    return true;
                }
            }
        }

        return false;
    }

    void ensure_goal_art()
    {
        if (ttrial_goal_checked)
            return;

        ttrial_goal_checked = true;
        find_goal_piece(5, ttrial_goal_bottom);
        find_goal_piece(6, ttrial_goal_top);
    }

    void apply_ttrial_goal_art()
    {
        if (!ttrial_final_approach())
            return;

        ensure_goal_art();
        if (!ttrial_goal_bottom.valid && !ttrial_goal_top.valid)
            return;

        // Re-skin the already positioned final checkpoint/direction sign rather
        // than creating a new free-floating sprite. This preserves the game's
        // own zoom, road attachment and timing on every selectable TT course.
        for (uint8_t i = 0; i < osprites.no_sprites; ++i)
        {
            oentry* sprite = &osprites.jump_table[i];
            if (!(sprite->control & OSprites::ENABLE))
                continue;

            const GoalPiece* goal = nullptr;
            if (sprite->function_holder == 5 && ttrial_goal_bottom.valid)
                goal = &ttrial_goal_bottom;
            else if (sprite->function_holder == 6 && ttrial_goal_top.valid)
                goal = &ttrial_goal_top;

            if (!goal)
                continue;

            sprite->addr = goal->addr;
            sprite->pal_src = goal->palette;
            sprite->pal_dst = 0;
            osprites.map_palette(sprite);
        }
    }

    void draw_frozen_ttrial_outro()
    {
        oentry* sprites[] =
        {
            oanimseq.anim_ferrari.sprite,
            oanimseq.anim_obj3.sprite,
            oanimseq.anim_pass1.sprite,
            oanimseq.anim_obj4.sprite,
            oanimseq.anim_pass2.sprite,
            oanimseq.anim_obj5.sprite,
        };

        for (oentry* sprite : sprites)
        {
            if (!sprite || !(sprite->control & OSprites::ENABLE) || !sprite->addr)
                continue;

            osprites.map_palette(sprite);
            osprites.do_spr_order_shadows(sprite);
        }
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
    // Once the game has returned to the attract/front-end sequence, discard
    // any temporary Music Select/race colour and restore the saved default.
    if (outrun.game_state >= GS_INIT && outrun.game_state <= GS_LOGO)
    {
        const int color =
            car_palette_state::get_default(config.engine.car_pal);
        config.engine.car_pal = color;
        ferrari_pal = car_palette_state::palette_source(color);
    }

    if (car_palette_hotkey::pressed())
    {
        cycle_car_palette();

        // Only F10 changes made while the actual attract drive is running
        // become the persistent default. In-game F10 changes stay temporary.
        if (outrun.game_state == GS_ATTRACT)
        {
            car_palette_state::set_default(config.engine.car_pal);
            config.save();
        }
    }

    apply_ttrial_goal_art();

    const bool ttrial_finish =
        ttrial_run_complete() && outrun.game_state == GS_BONUS;

    if (ttrial_finish && !ttrial_outro_active)
    {
        // Reuse the real ROM-driven Ferrari ending animation, but keep the
        // existing Time Trial road. Sequence 0 provides the classic sideways
        // braking/turn-in used when the Ferrari reaches an OutRun goal.
        oanimseq.end_seq = 0;
        oanimseq.init_end_seq();
        ttrial_outro_active = true;
    }

    if (ttrial_outro_active && ttrial_run_complete() &&
        outrun.game_state >= GS_INIT_GAMEOVER &&
        outrun.game_state <= GS_GAMEOVER)
    {
        // Results should not restart or continue the celebration. Hold the last
        // Ferrari/occupant pose behind the results page instead.
        draw_frozen_ttrial_outro();
        return;
    }

    tick_base();

    if (!ttrial_run_complete() && ttrial_outro_active)
        ttrial_outro_active = false;
}

/***************************************************************************
    Ferrari palette extensions for CannonBall-SE.

    The original Ferrari implementation is kept verbatim in oferrari_base.cpp.
    This wrapper extends its palette list and handles live F10 colour cycling
    during normal Ferrari updates. Attract mode shares the same edge state via
    car_palette_hotkey.hpp so the key can also be handled there safely.
***************************************************************************/

#include "../trackloader.hpp"
#include "../roms.hpp"
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
    const uint8_t TTRIAL_GOAL_BOTTOM_SLOT = OSprites::SPRITE_ENTRIES + 22;
    const uint8_t TTRIAL_GOAL_TOP_SLOT    = OSprites::SPRITE_ENTRIES + 23;

    const uint16_t TTRIAL_GOAL_FAR_Z      = 0x20;
    const uint16_t TTRIAL_GOAL_FINISH_Z   = 0x1D0;
    const uint16_t TTRIAL_GOAL_DISTANCE   = 0x140;
    const uint16_t TTRIAL_CAR_START_Z     = 0x1FC;
    const uint16_t TTRIAL_CAR_END_Z       = 0x20;

    bool ttrial_goal_ready = false;
    bool ttrial_finish_visual = false;
    int ttrial_finish_total_ticks = 0;

    int16_t ttrial_pass1_dx = 0;
    int16_t ttrial_pass1_dy = 0;
    int16_t ttrial_pass2_dx = 0;
    int16_t ttrial_pass2_dy = 0;

    bool load_ttrial_goal_piece(oentry* sprite, uint8_t slot, int wanted_routine)
    {
        // The original GOAL/checkpoint art is stored as normal scenery data in
        // the five end sections. Find the two sign components by their stock
        // checkpoint routines (5 = lower part, 6 = upper part) instead of
        // hard-coding sprite graphics or palette addresses.
        for (int ending = 0; ending < 5; ++ending)
        {
            const uint32_t end_section = roms.rom0p->read32(
                outrun.adr.road_seg_end + (ending << 2));
            const uint32_t scenery_addr = roms.rom0p->read32(end_section + 8);
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

                // Frequency is irrelevant here. The second word is the last
                // eight-byte sprite definition offset in the pattern.
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

                    sprite->init(slot);
                    sprite->control = OSprites::ENABLE;

                    if (props & BIT_0)
                        sprite->control |= OSprites::HFLIP;
                    if (props & BIT_1)
                        sprite->control |= OSprites::SHADOW;

                    sprite->function_holder = static_cast<int8_t>(routine);
                    sprite->draw_props = static_cast<uint8_t>((props & 0xF0) | oentry::BOTTOM);
                    sprite->shadow = 7;
                    sprite->xw1 = sprite->xw2 = static_cast<int16_t>(
                        static_cast<int8_t>(trackloader.scenerymap_data[entry + 1]) << 4);
                    sprite->yw = static_cast<uint16_t>(
                        ((trackloader.scenerymap_data[entry + 2] << 8) |
                         trackloader.scenerymap_data[entry + 3]) << 7);
                    sprite->type = static_cast<uint16_t>(
                        trackloader.scenerymap_data[entry + 5] << 2);
                    sprite->addr = roms.rom0p->read32(
                        outrun.adr.sprite_type_table + sprite->type);
                    sprite->pal_src = trackloader.scenerymap_data[entry + 7];
                    sprite->width = 0;
                    sprite->hidden = 0;
                    osprites.map_palette(sprite);
                    return true;
                }
            }
        }

        sprite->control &= ~OSprites::ENABLE;
        return false;
    }

    bool ensure_ttrial_goal()
    {
        if (ttrial_goal_ready)
            return true;

        oentry* bottom = &osprites.jump_table[TTRIAL_GOAL_BOTTOM_SLOT];
        oentry* top    = &osprites.jump_table[TTRIAL_GOAL_TOP_SLOT];

        const bool have_bottom = load_ttrial_goal_piece(
            bottom, TTRIAL_GOAL_BOTTOM_SLOT, 5);
        const bool have_top = load_ttrial_goal_piece(
            top, TTRIAL_GOAL_TOP_SLOT, 6);

        ttrial_goal_ready = have_bottom && have_top;
        if (!ttrial_goal_ready)
        {
            bottom->control &= ~OSprites::ENABLE;
            top->control &= ~OSprites::ENABLE;
        }
        return ttrial_goal_ready;
    }

    void reset_ttrial_goal()
    {
        osprites.jump_table[TTRIAL_GOAL_BOTTOM_SLOT].control &= ~OSprites::ENABLE;
        osprites.jump_table[TTRIAL_GOAL_TOP_SLOT].control &= ~OSprites::ENABLE;
        ttrial_goal_ready = false;
        ttrial_finish_visual = false;
        ttrial_finish_total_ticks = 0;
    }

    void draw_ttrial_goal_piece(oentry* sprite, uint16_t z16)
    {
        if (!(sprite->control & OSprites::ENABLE))
            return;

        if (z16 < 4)
            z16 = 4;
        else if (z16 > 0x1FC)
            z16 = 0x1FC;

        sprite->z = static_cast<int32_t>(z16) << 16;
        sprite->road_priority = z16;
        sprite->priority = z16;
        sprite->zoom = static_cast<uint8_t>(z16 >> 1);

        int32_t road_y = -(oroad.road_y[oroad.road_p0 + z16] >> 4) + 223;
        if (sprite->yw != 0)
        {
            uint32_t yw = sprite->yw * z16;
            outils::swap32(yw);
            outils::sub16(static_cast<int32_t>(yw), road_y);
        }
        sprite->y = static_cast<int16_t>(road_y);

        int16_t road_x = oroad.road0_h[z16];
        int16_t xw1 = sprite->xw1;
        if (xw1 >= 0)
            xw1 += static_cast<int16_t>((oroad.road_width >> 16) << 1);

        sprite->x = static_cast<int16_t>(
            road_x + ((static_cast<int32_t>(xw1) * z16) >> 9));

        osprites.do_spr_order_shadows(sprite);
    }

    void draw_ttrial_goal(uint16_t z16)
    {
        if (!ensure_ttrial_goal())
            return;

        draw_ttrial_goal_piece(
            &osprites.jump_table[TTRIAL_GOAL_BOTTOM_SLOT], z16);
        draw_ttrial_goal_piece(
            &osprites.jump_table[TTRIAL_GOAL_TOP_SLOT], z16);
    }

    uint16_t ttrial_goal_approach_z()
    {
        const int32_t pos = static_cast<int32_t>(oroad.road_pos >> 16);
        const int32_t start = static_cast<int32_t>(ROAD_END) - TTRIAL_GOAL_DISTANCE;

        if (pos <= start)
            return TTRIAL_GOAL_FAR_Z;
        if (pos >= ROAD_END)
            return TTRIAL_GOAL_FINISH_Z;

        const int32_t travelled = pos - start;
        const int32_t range = TTRIAL_GOAL_FINISH_Z - TTRIAL_GOAL_FAR_Z;
        return static_cast<uint16_t>(
            TTRIAL_GOAL_FAR_Z + (travelled * range) / TTRIAL_GOAL_DISTANCE);
    }

    void draw_receding_sprite(
        oentry* sprite,
        int16_t x,
        int16_t y,
        uint16_t z16,
        uint8_t zoom,
        uint16_t priority)
    {
        if (!(sprite->control & OSprites::ENABLE))
            return;

        sprite->x = x;
        sprite->y = y;
        sprite->z = static_cast<int32_t>(z16) << 16;
        sprite->zoom = zoom;
        sprite->road_priority = priority;
        sprite->priority = priority;
        osprites.map_palette(sprite);
        osprites.do_spr_order_shadows(sprite);
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

    const bool ttrial_final_approach =
        outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
        outrun.ttrial.laps != 0 &&
        outrun.game_state == GS_INGAME &&
        static_cast<int>(outrun.ttrial.current_lap) + 1 >=
            static_cast<int>(outrun.ttrial.laps);

    const bool ttrial_finish =
        outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
        outrun.ttrial.laps != 0 &&
        outrun.ttrial.current_lap >= outrun.ttrial.laps &&
        outrun.game_state == GS_BONUS;

    // After the final line the road/camera remains exactly where it was. The
    // Ferrari itself is then drawn progressively deeper into that frozen road,
    // giving the impression that it simply drives away through the GOAL sign.
    if (ttrial_finish)
    {
        if (!ttrial_finish_visual)
        {
            ttrial_finish_visual = true;
            ttrial_finish_total_ticks = config.tick_fps * 2 + 5;
            obonus.bonus_timer = static_cast<int16_t>(ttrial_finish_total_ticks);

            ttrial_pass1_dx = static_cast<int16_t>(spr_pass1->x - spr_ferrari->x);
            ttrial_pass1_dy = static_cast<int16_t>(spr_pass1->y - spr_ferrari->y);
            ttrial_pass2_dx = static_cast<int16_t>(spr_pass2->x - spr_ferrari->x);
            ttrial_pass2_dy = static_cast<int16_t>(spr_pass2->y - spr_ferrari->y);

            // The normal shadow is tied to the fixed chase-camera Ferrari size;
            // suppress it during this tiny perspective-only finish shot.
            spr_shadow->control &= ~OSprites::ENABLE;
        }

        oinitengine.car_increment = 0;
        car_inc_old = 0;
        car_ctrl_active = false;
        oinputs.acc_adjust = 0;
        oinputs.brake_adjust = 0;
        oinputs.steering_adjust = 0;

        draw_ttrial_goal(TTRIAL_GOAL_FINISH_Z);

        int elapsed = ttrial_finish_total_ticks - obonus.bonus_timer;
        if (elapsed < 0)
            elapsed = 0;
        if (elapsed > ttrial_finish_total_ticks)
            elapsed = ttrial_finish_total_ticks;

        const uint16_t z16 = static_cast<uint16_t>(
            TTRIAL_CAR_START_Z -
            ((TTRIAL_CAR_START_Z - TTRIAL_CAR_END_Z) * elapsed) /
                ttrial_finish_total_ticks);

        int16_t car_y = static_cast<int16_t>(
            -(oroad.road_y[oroad.road_p0 + z16] >> 4) + 223);
        int16_t car_x = oroad.road0_h[z16];
        uint8_t car_zoom = static_cast<uint8_t>(z16 >> 2);
        if (car_zoom < 8)
            car_zoom = 8;

        const int32_t scale = (static_cast<int32_t>(car_zoom) << 8) / 0x7F;

        spr_ferrari->draw_props = oentry::BOTTOM;
        draw_receding_sprite(
            spr_ferrari,
            car_x,
            car_y,
            z16,
            car_zoom,
            z16);

        draw_receding_sprite(
            spr_pass1,
            static_cast<int16_t>(car_x + ((ttrial_pass1_dx * scale) >> 8)),
            static_cast<int16_t>(car_y + ((ttrial_pass1_dy * scale) >> 8)),
            z16,
            car_zoom,
            static_cast<uint16_t>(z16 + 1));

        draw_receding_sprite(
            spr_pass2,
            static_cast<int16_t>(car_x + ((ttrial_pass2_dx * scale) >> 8)),
            static_cast<int16_t>(car_y + ((ttrial_pass2_dy * scale) >> 8)),
            z16,
            car_zoom,
            static_cast<uint16_t>(z16 + 1));

        return;
    }

    if (ttrial_finish_visual)
        reset_ttrial_goal();

    tick_base();

    // Place the original GOAL sign on the last Time Trial lap without loading
    // any end road. It grows toward the camera as ROAD_END approaches and then
    // remains frozen for the short drive-away shot above.
    if (ttrial_final_approach)
    {
        const int32_t pos = static_cast<int32_t>(oroad.road_pos >> 16);
        if (pos >= static_cast<int32_t>(ROAD_END) - TTRIAL_GOAL_DISTANCE)
            draw_ttrial_goal(ttrial_goal_approach_z());
    }
    else if (ttrial_goal_ready)
    {
        reset_ttrial_goal();
    }
}

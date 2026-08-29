/***************************************************************************
    CannonBall DX multiplayer traffic bridge.

    Player 1 runs the unchanged OutRun traffic simulation. Player 2 skips its
    local traffic tick once authoritative snapshots are available and projects
    Player 1's eight traffic slots through Player 2's own road/camera state.
***************************************************************************/

#pragma once

#include <algorithm>
#include <cstdint>

#include "engine/multiplayer.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/otraffic.hpp"

namespace multiplayer_traffic
{
    inline uint8_t derive_palette_cycle()
    {
        for (std::size_t slot = 0; slot < multiplayer_detail::TRAFFIC_SLOTS; slot++)
        {
            const uint8_t index = static_cast<uint8_t>(OSprites::SPRITE_TRAFF1 + slot);
            const oentry& sprite = osprites.jump_table[index];
            if (!(sprite.control & OSprites::ENABLE))
                continue;

            const uint8_t base =
                roms.rom0p->read8(outrun.adr.traffic_props + sprite.type + 4);
            if (sprite.pal_src >= base)
                return static_cast<uint8_t>((sprite.pal_src - base) & 1);
        }
        return 0;
    }

    inline void capture_after_local_tick()
    {
        if (multiplayer.traffic_authority())
            multiplayer.capture_local_traffic(derive_palette_cycle());
    }

    inline void player2_collision(oentry* sprite)
    {
        if (!outrun.tick_frame || outrun.game_state != GS_INGAME || sprite->hidden > 0)
            return;

        int16_t d0 = 0;
        if ((sprite->z >> 16) >= 0x1D8)
        {
            const int16_t w =
                (sprite->width >> 1) + (sprite->width >> 3) + (sprite->width >> 4);
            const int16_t x1 = sprite->x - w;
            const int16_t x2 = sprite->x + w;

            if (x1 < 0 && x2 > 0)
            {
                otraffic.collision_mask =
                    roms.rom0p->read8(outrun.adr.traffic_props + sprite->type + 5);

                d0 = (sprite->x < 0) ? -OCrash::SKID_RESET : OCrash::SKID_RESET;
                d0 += ocrash.skid_counter;
                if (d0 <= OCrash::SKID_MAX && d0 >= -OCrash::SKID_MAX)
                    ocrash.skid_counter = d0;

                int16_t traffic_speed = sprite->traffic_speed - 80;
                if (traffic_speed < 0)
                    traffic_speed = 0;

                oinitengine.car_increment =
                    (static_cast<uint32_t>(traffic_speed) << 16) |
                    (oinitengine.car_increment & 0xFFFF);
                oferrari.car_inc_old = traffic_speed;
                d0 = sound::REBOUND;
                otraffic.collision_traffic++;
                outrun.ttrial.vehicle_cols++;
            }
        }

        const uint8_t old_fx = sprite->traffic_fx;
        sprite->traffic_fx = static_cast<uint8_t>(d0 & 0xFF);
        if (!old_fx && sprite->traffic_fx)
            osoundint.queue_sound(sprite->traffic_fx);
    }

    inline void set_zoom(oentry* sprite)
    {
        uint16_t zoom = (sprite->road_priority >> 2) + 4;
        if (zoom > 0x7F)
            zoom = 0x7F;

        const uint8_t zoom_lookup =
            roms.rom0p->read8(outrun.adr.traffic_props + sprite->type + 6);
        switch (zoom_lookup)
        {
            case 0: zoom += (zoom >> 3); break;
            case 2: zoom += (zoom >> 2); break;
            case 4: zoom += (zoom >> 1); break;
            case 6: zoom += zoom; break;
        }
        sprite->zoom = static_cast<uint8_t>(zoom);
    }

    inline bool render_remote()
    {
        if (!multiplayer.use_remote_traffic())
            return false;

        const int64_t player_road_delta =
            static_cast<int64_t>(multiplayer.traffic_authority_road_pos()) -
            static_cast<int64_t>(static_cast<int32_t>(oroad.road_pos));
        const int32_t depth_delta =
            static_cast<int32_t>((player_road_delta * 8) >> 16);
        const uint8_t palette_cycle = multiplayer.remote_traffic_palette_cycle();

        for (std::size_t slot = 0; slot < multiplayer_detail::TRAFFIC_SLOTS; slot++)
        {
            const uint8_t index = static_cast<uint8_t>(OSprites::SPRITE_TRAFF1 + slot);
            oentry* sprite = &osprites.jump_table[index];
            const auto remote = multiplayer.remote_traffic(slot);

            if (!remote.enabled)
            {
                sprite->control &= ~OSprites::ENABLE;
                continue;
            }

            sprite->control = OSprites::ENABLE | OSprites::TRAFFIC_SPRITE | OSprites::SHADOW;
            if (remote.rhs)
                sprite->control |= OSprites::TRAFFIC_RHS;

            sprite->draw_props = oentry::BOTTOM;
            sprite->shadow = 7;
            sprite->type = remote.type;
            sprite->xw1 = remote.xw1;
            sprite->xw2 = remote.xw2;
            sprite->traffic_speed = remote.speed;
            sprite->traffic_orig_speed = remote.orig_speed;
            sprite->traffic_proximity = 0;
            sprite->hidden = remote.hidden;

            // Traffic z is player-relative in the original engine. Convert the
            // Player-1-relative snapshot into Player 2's current road position.
            const int64_t local_z = static_cast<int64_t>(remote.z) -
                                    (static_cast<int64_t>(depth_delta) << 16);
            if (local_z <= 0 || local_z >= (static_cast<int64_t>(0x200) << 16))
            {
                sprite->control &= ~OSprites::ENABLE;
                continue;
            }
            sprite->z = static_cast<int32_t>(local_z);

            const uint16_t z16 = static_cast<uint16_t>(sprite->z >> 16);
            sprite->priority = sprite->road_priority = z16;
            sprite->y = -(oroad.road_y[oroad.road_p0 + z16] >> 4) + 223;
            set_zoom(sprite);

            int16_t* road_x =
                (sprite->control & OSprites::TRAFFIC_RHS) ? oroad.road1_h : oroad.road0_h;
            int32_t x = (sprite->xw1 * z16) >> 9;
            sprite->x = static_cast<int16_t>(x + road_x[z16]);

            if (z16 <= 8)
            {
                sprite->pal_src =
                    roms.rom0p->read8(outrun.adr.traffic_props + sprite->type + 4) +
                    palette_cycle;
                osprites.map_palette(sprite);
                osprites.do_spr_order_shadows(sprite);
                continue;
            }

            int16_t y = 0;
            if (oroad.road_p0 > (0x10 / 2))
                y = oroad.road_y[oroad.road_p0 - (0x10 / 2)] - oroad.road_y[oroad.road_p0];
            const int8_t incline = (y < 0x12) ? 0x10 : 0;

            x = oinitengine.car_x_pos - (oroad.road_width >> 16);
            if (sprite->control & OSprites::TRAFFIC_RHS)
                x += (oroad.road_width >> 16) << 1;
            x += oroad.road_x[z16] - oroad.road_x[z16 - (0x10 / 2)];
            x = std::max<int32_t>(-0x190, std::min<int32_t>(0x190, x));
            x = (x >> 2) + (sprite->xw1 >> 2);

            int8_t traffic_frame = 3;
            const int32_t xabs = x < 0 ? -x : x;
            if (xabs < 0x10) traffic_frame = 1;
            else if (xabs < 0x30) traffic_frame = 2;

            if (x < 0)
                sprite->control &= ~OSprites::HFLIP;
            else
                sprite->control |= OSprites::HFLIP;

            sprite->pal_src =
                roms.rom0p->read8(outrun.adr.traffic_props + sprite->type + 4) +
                palette_cycle;
            const int16_t traffic_type =
                (roms.rom0p->read8(outrun.adr.traffic_props + sprite->type + 7) << 5) +
                (traffic_frame << 2) + incline;
            sprite->addr = roms.rom0p->read32(outrun.adr.traffic_data + traffic_type);

            // Local collision is evaluated against the shared car's locally
            // projected position. Player 2's impact currently affects Player 2
            // only; forwarding that impact to the shared traffic authority is
            // the next network event layer.
            player2_collision(sprite);

            osprites.map_palette(sprite);
            osprites.do_spr_order_shadows(sprite);
        }

        return true;
    }
}

/***************************************************************************
    Traffic Routines.

    - Traffic spawning.
    - Traffic logic, lane changing & movement.
    - Collisions.
    - Traffic panning and volume control to pass to sound program.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include "outrun.hpp"

class OTraffic
{
public:
    uint8_t ai_traffic;
    uint8_t bonus_lhs;
    int8_t traffic_split;

    uint16_t collision_traffic;
    uint16_t collision_mask;

    OTraffic(void);
    ~OTraffic(void);
    void init();
    void init_stage1_traffic();
    void tick();
    void disable_traffic();
    void set_max_traffic();
    void traffic_logic();
    void traffic_sound();

    void set_custom_max_traffic(uint8_t value) { max_traffic = value; }

private:
    enum
    {
        TRAFFIC_INIT = 0x10,
        TRAFFIC_ENTRY = 0x11,
        TRAFFIC_TICK = 0x12
    };

    oentry* traffic_adr[9];
    uint8_t max_traffic;
    int16_t traffic_speed_total;
    int16_t traffic_speed_avg;
    uint8_t traffic_pal_cycle;
    int16_t traffic_count;
    int16_t spawn_counter;
    int16_t spawn_location;
    int16_t wheel_reset;
    int16_t wheel_counter;

    void spawn_car(oentry* sprite);
    void spawn_traffic();
    void tick_spawned_sprite(oentry* sprite);
    void move_spawned_sprite(oentry* sprite);

    // advance=false is used on Player 2 for Player-1-authoritative traffic:
    // keep the transmitted world state fixed and only project it through the
    // local camera. Overtake scoring is also suppressed on that render-only path.
    void update_props(oentry* sprite, bool advance = true);
    void render_multiplayer_traffic();

    void set_zoom_lookup(oentry* sprite);
    void calculate_avg_speed(uint16_t);
    void check_collision(oentry* sprite);
};

extern OTraffic otraffic;

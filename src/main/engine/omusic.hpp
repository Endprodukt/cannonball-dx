/***************************************************************************
    Music Selection Screen.

    This is a combination of a tilemap and overlayed sprites.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include "outrun.hpp"

class RomLoader;

class OMusic
{
public:
    OMusic(void);
    ~OMusic(void);

    bool load_widescreen_map(std::string path);
    void enable();
    void enable_base();
    void disable();
    void tick();
    void tick_base();
    void blit();
    void check_start();
    void check_start_base();
    void play_music(int index = -1);
    void cycle_music();
    void cycle_music_base();

    // Called by the post-output FFB hook in main.cpp. This now exposes the
    // real selected song without applying a continuous holding force. main.cpp
    // uses selection changes to generate only the short detent step pulses.
    int get_music_selected();
    int get_music_selected_base();

    // Read-only selector position for non-FFB consumers such as gamepad
    // rumble. This exposes the real cursor and has no side effects on FFB.
    int get_music_position() const { return cursor_pos; }

    // Selected game mode while the music screen is active. External cabinet
    // outputs use this to blink only the matching VIEW 1/2/3 lamp. Endless is
    // intentionally reported as Continuous because both live on VIEW2.
    int get_game_mode() const { return game_mode_selected; }

    // The Time Trial course selector can be cancelled back to the frontend.
    // Clear the one-shot handoff so a later Time Trial cannot accidentally
    // reuse the music selection from the cancelled run.
    void cancel_time_trial_from_music()
    {
        return_from_time_trial = false;
        skip_music_tick = false;
    }

private:
    // Modified Widescreen version of the Music Select Tilemap
    RomLoader* tilemap;
    // Additional Widescreen tiles
    RomLoader* tile_patch;

    // Next track to play
    music_t* next_track;

    // Music Track Selected By Player
    uint8_t music_selected;

    // Total tracks to include in music select (> 3 means user has added extra ones)
    int total_tracks;

    // Enahcned: Current Cursor Position
    int cursor_pos;

    uint16_t entry_start;

    // Used to preview music track
    int16_t last_music_selected;
    int8_t preview_counter;

    // Game mode chosen on the music-selection screen. VIEW cycles this value;
    // VIEW1/2/3 select Original/Continuous/Time Trial directly.
    int game_mode_selected;

    // VIEW2 has a second state: Continuous -> Endless. Internally Endless
    // keeps MODE_CONT so the existing Continuous engine path can be extended
    // without creating a fourth cabinet-view output.
    bool endless_selected = false;

    // Gear/shifter state used to turn LOW/HIGH movement into previous/next
    // Ferrari colour selection without affecting the in-race gear logic.
    bool menu_gear_initialized;
    bool menu_gear_state;

    // Time Trial normally flows Track Select -> Music Select -> Race. When it
    // was entered from this music screen we remember the chosen song and skip
    // that second music screen after the course has been selected.
    bool return_from_time_trial;
    bool skip_music_tick;
    int pending_music_selected;

    // Preserved legacy virtual-detent state. The active DX Music Select path no
    // longer uses this to hold the wheel at song positions; only transient step
    // pulses are generated when the selected song changes.
    int ffb_detent_spring_applied;
    int ffb_detent_target_applied;

    const static short HAND_LEFT = 0, HAND_CENTRE = 1, HAND_RIGHT = 2;
    
	void setup_sprite1();
	void setup_sprite2();
	void setup_sprite3();
	void setup_sprite4();
	void setup_sprite5();
    void tick_original(oentry*, oentry*, oentry*);
    void tick_enhanced(oentry*, oentry*, oentry*);
    void set_hand(short, oentry*, oentry*, oentry*);
    void blit_music_select();

    int track_from_steering(int steering) const;
    int steering_for_track(int track) const;
    void apply_music_detent_ffb();
    void reset_music_detent_ffb();

    void set_game_mode(int mode);
    void cycle_game_mode();
    void cycle_car_color(int direction);
    void set_continuous_traffic_from_difficulty();
    void draw_game_options();
    void draw_color_swatch(uint16_t x, uint16_t y);
};

extern OMusic omusic;
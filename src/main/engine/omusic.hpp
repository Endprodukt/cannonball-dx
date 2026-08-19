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
    void disable();
    void tick();
    void blit();
    void check_start();
    void play_music(int index = -1);
    void cycle_music();

    // Called by the existing post-output FFB hook in main.cpp. The actual
    // music selection is handled inside OMusic; this call refreshes the
    // continuous selector detent and deliberately returns a stable value so
    // the older timed music-kick code remains inactive.
    int get_music_selected();

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

    // Music-selector FFB tracking. The spring is made progressively stronger
    // when many tracks are present so closely spaced virtual detents remain
    // distinguishable.
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
};

extern OMusic omusic;


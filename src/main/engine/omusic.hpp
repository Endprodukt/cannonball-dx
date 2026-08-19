/***************************************************************************
    Music Selection Screen.

    This is a combination of a tilemap and overlayed sprites.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include "outrun.hpp"
#include "engine/oinputs.hpp"
#include "directx/ffeedback.hpp"

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

    // Called by the post-output FFB hook while the music-selection screen is active.
    // Instead of a short timed pulse, create three stable steering zones: the normal
    // centering spring holds the middle position, while a continuous low constant
    // force biases the wheel outward in the left/right zones. The spring and the
    // outward bias balance each other like a mechanical selector detent.
    int get_music_selected() const
    {
        if (config.controls.haptic && forcefeedback::is_supported())
        {
            const int steering = static_cast<int>(oinputs.steering_adjust);
            const int spring_strength = config.controls.centering_strength;

            if (spring_strength > 0)
            {
                // Keep the bias proportional to the native spring. With the default
                // 30% spring this gives roughly 20% constant-force gain, enough to
                // form a distinct side equilibrium without dragging the wheel to lock.
                int detent_gain = (spring_strength * 2 + 1) / 3;
                if (detent_gain < 10)
                    detent_gain = 10;
                else if (detent_gain > 100)
                    detent_gain = 100;

                if (steering <= -43)
                {
                    forcefeedback::set_gain(detent_gain);
                    forcefeedback::set(0x09, 7); // Hold the left selector position.
                    forcefeedback::set_gain(config.controls.ffb_strength);
                }
                else if (steering >= 43)
                {
                    forcefeedback::set_gain(detent_gain);
                    forcefeedback::set(0x07, 7); // Hold the right selector position.
                    forcefeedback::set_gain(config.controls.ffb_strength);
                }
                else
                {
                    // Centre selector position: let the native spring do the work.
                    forcefeedback::stop();
                }
            }
            else
            {
                forcefeedback::stop();
            }
        }

        // The previous event-based detector in main.cpp is intentionally kept quiet.
        // The actual selected track remains stored in music_selected and is used by
        // OMusic itself; this accessor exists only for the FFB hook.
        return 0;
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
};

extern OMusic omusic;


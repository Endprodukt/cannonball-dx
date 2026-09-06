#pragma once

// Prevent Windows headers from defining min/max macros, and remove a max macro
// if one was already defined before this header was reached.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef max
#undef max
#endif

#include "frontend/ttrial.hpp"

// Keep the proven side renderer untouched under a private namespace, then
// append only the pixel-exact correction regions from the user's BMP.
#define music_side_art music_side_art_original
#include "music_side_art_base.hpp"
#undef music_side_art

#include "music_side_art_corrections.hpp"
#include "music_side_art_final_line.hpp"

namespace music_side_art
{
    inline void render(uint16_t* buffer)
    {
        // The Time Trial course selector is entered from the Music Select flow,
        // and Outrun::game_state can still report GS_MUSIC while the frontend
        // selector owns the screen. Without this explicit guard the Music
        // Select side art is drawn again after the map sprites every frame,
        // reintroducing the steering wheel/console fragments in the margins.
        if (time_trial_selector_active())
            return;

        // The embedded art renderer contains a one-shot development diagnostic.
        // Mark it as already logged so release builds stay quiet without changing
        // any decoding, palette mapping or rendering behaviour.
        music_side_art_original::get_state().logged = true;

        music_side_art_original::render(buffer);
        music_side_art_corrections::render(buffer);
        music_side_art_final_line::render(buffer);
    }
}

/***************************************************************************
    Stereoscopic 3D ("layered stereo") support for CannonBall DX.
    See stereo.hpp for an overview.
***************************************************************************/

#include <cstring>
#include <iostream>

#include "stereo.hpp"
#include "frontend/config.hpp"

namespace stereo
{
    State   state;
    Samples samples;

    bool enabled()
    {
        return config.video.stereo > MODE_OFF && config.video.stereo < MODE_COUNT;
    }

    void begin_frame()
    {
        state.eye        = EYE_NONE;
        state.separation = static_cast<float>(std::clamp(config.video.stereo_depth, 0, 48));
        state.inv_max    = 1.0f + std::clamp(config.video.stereo_popout, 0, 200) / 100.0f;
    }

    void compose(uint16_t* out, const uint16_t* left, const uint16_t* right,
                 int width, int height, int mode)
    {
        if (!out || !left || !right || width <= 1 || height <= 1)
            return;

        // Palette indices cannot be blended, so each eye is point-sampled to
        // half resolution. Use a 2x or higher engine resolution so each eye
        // keeps the full native arcade detail.
        if (mode == MODE_TAB)
        {
            const int top_h = height / 2;
            const int bot_h = height - top_h;
            for (int y = 0; y < top_h; ++y)
                std::memcpy(out + y * width, left + ((y * height) / top_h) * width,
                            width * sizeof(uint16_t));
            for (int y = 0; y < bot_h; ++y)
                std::memcpy(out + (top_h + y) * width, right + ((y * height) / bot_h) * width,
                            width * sizeof(uint16_t));
            return;
        }

        // Half side-by-side (optionally swapped for cross-eye viewing).
        const bool swap   = (mode == MODE_SBS_CROSS);
        const uint16_t* first  = swap ? right : left;
        const uint16_t* second = swap ? left  : right;
        const int half_w = width / 2;
        const int rest_w = width - half_w;

        for (int y = 0; y < height; ++y)
        {
            uint16_t* dst = out + y * width;
            const uint16_t* a = first  + y * width;
            const uint16_t* b = second + y * width;

            for (int x = 0; x < half_w; ++x)
                dst[x] = a[(x * width) / half_w];
            for (int x = 0; x < rest_w; ++x)
                dst[half_w + x] = b[(x * width) / rest_w];
        }
    }

    const char* mode_name(int mode)
    {
        switch (mode)
        {
            case MODE_OFF:       return "OFF";
            case MODE_SBS:       return "HALF SIDE-BY-SIDE";
            case MODE_SBS_CROSS: return "HALF SIDE-BY-SIDE (CROSS-EYE)";
            case MODE_TAB:       return "HALF TOP-AND-BOTTOM";
            default:             return "UNKNOWN";
        }
    }

    namespace
    {
        const int DEPTH_STEPS[]  = { 4, 6, 8, 10, 12, 16, 20, 24 };
        const int POPOUT_STEPS[] = { 0, 25, 50, 75, 100 };

        // Next/previous entry of 'steps' relative to 'value' (which need not
        // be one of the steps, e.g. a hand-edited config value), wrapping.
        template <size_t N>
        int step_in(const int (&steps)[N], int value, int direction)
        {
            if (direction >= 0)
            {
                for (int s : steps)
                    if (s > value) return s;
                return steps[0];
            }
            for (int i = static_cast<int>(N) - 1; i >= 0; --i)
                if (steps[i] < value) return steps[i];
            return steps[N - 1];
        }
    }

    void step_mode(int direction)
    {
        const int n = MODE_COUNT;
        config.video.stereo = ((config.video.stereo + (direction >= 0 ? 1 : -1)) % n + n) % n;
        std::cout << "Stereo 3D: " << mode_name(config.video.stereo) << std::endl;
    }

    void step_depth(int direction)
    {
        config.video.stereo_depth = step_in(DEPTH_STEPS, config.video.stereo_depth, direction);
        std::cout << "Stereo 3D depth: " << config.video.stereo_depth << std::endl;
    }

    void step_popout(int direction)
    {
        config.video.stereo_popout = step_in(POPOUT_STEPS, config.video.stereo_popout, direction);
        std::cout << "Stereo 3D pop-out: " << config.video.stereo_popout << "%" << std::endl;
    }

    void cycle_mode()  { step_mode(1); }
    void cycle_depth() { step_depth(1); }

    std::string menu_mode_text()
    {
        const char* value = "OFF";
        switch (config.video.stereo)
        {
            case MODE_SBS:       value = "SIDE BY SIDE"; break;
            case MODE_SBS_CROSS: value = "CROSS EYE";    break;
            case MODE_TAB:       value = "TOP BOTTOM";   break;
            default:             break;
        }
        return std::string(MENU_MODE_LABEL) + value;
    }

    std::string menu_depth_text()
    {
        return std::string(MENU_DEPTH_LABEL) + std::to_string(config.video.stereo_depth);
    }

    std::string menu_popout_text()
    {
        return std::string(MENU_POPOUT_LABEL) + std::to_string(config.video.stereo_popout);
    }
}

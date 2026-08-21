#pragma once

#include <atomic>

namespace pixel_scaler
{
    enum Mode
    {
        OFF = 0,
        XBRZ_2X,
        XBRZ_3X,
        XBRZ_4X,
        XBRZ_5X,
        XBRZ_6X,
        HQX_2X,
        HQX_3X,
        HQX_4X,
        MODE_COUNT,
    };

    inline std::atomic<int> mode{OFF};
    inline std::atomic<int> last_mode{XBRZ_4X};

    inline bool valid(int value)
    {
        return value >= OFF && value < MODE_COUNT;
    }

    inline bool active(int value)
    {
        return value > OFF && value < MODE_COUNT;
    }

    inline bool is_xbrz(int value)
    {
        return value >= XBRZ_2X && value <= XBRZ_6X;
    }

    inline bool is_hqx(int value)
    {
        return value >= HQX_2X && value <= HQX_4X;
    }

    inline int factor(int value)
    {
        if (is_xbrz(value))
            return value - XBRZ_2X + 2;
        if (is_hqx(value))
            return value - HQX_2X + 2;
        return 1;
    }

    inline const char* name(int value)
    {
        switch (value)
        {
            case XBRZ_2X: return "XBRZ 2X";
            case XBRZ_3X: return "XBRZ 3X";
            case XBRZ_4X: return "XBRZ 4X";
            case XBRZ_5X: return "XBRZ 5X";
            case XBRZ_6X: return "XBRZ 6X";
            case HQX_2X:  return "HQX 2X";
            case HQX_3X:  return "HQX 3X";
            case HQX_4X:  return "HQX 4X";
            default:      return "OFF";
        }
    }

    inline void set(int value)
    {
        if (!valid(value))
            value = OFF;

        mode.store(value, std::memory_order_relaxed);
        if (active(value))
            last_mode.store(value, std::memory_order_relaxed);
    }

    inline int cycle()
    {
        int next = mode.load(std::memory_order_relaxed) + 1;
        if (next >= MODE_COUNT)
            next = OFF;

        set(next);
        return next;
    }
}

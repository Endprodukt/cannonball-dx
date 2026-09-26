#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// CannonBall DX experimental high-resolution road model.
//
// The arcade path remains untouched. This parallel path rebuilds only the visual
// horizontal road curve from the same track data, using floating-point curve
// anchors and a shape-preserving cubic interpolation between them. The result is
// converted back into a bounded high-resolution HScroll correction, so gameplay,
// collision and the low-resolution renderer continue to use the original values.

namespace
{
    constexpr int DX_HIRES_ROAD_SAMPLES = 0x200;
    constexpr int DX_HIRES_ROAD_FIRST_SAMPLE = 0x37E / 2; // 447, same as setup_x_data
    constexpr int DX_HIRES_ROAD_MAX_ANCHORS = 0x22;       // start + 0x21 path samples

    struct DxRoadCurveAnchor
    {
        double sample;
        double value;
    };

    bool dx_create_curve_integer(
        int32_t curve_x_total,
        int32_t curve_y_total,
        int16_t curve_x_dist,
        int16_t curve_y_dist,
        int16_t& curve_inc,
        int16_t& curve_end)
    {
        int32_t d0 = ((curve_x_total >> 5) * curve_y_dist) -
                     ((curve_y_total >> 5) * curve_x_dist);
        int32_t d2 = ((curve_x_total >> 5) * curve_x_dist) +
                     ((curve_y_total >> 5) * curve_y_dist);

        d2 >>= 7;
        const int32_t d1 = (d2 >> 7) + 0x410;
        if (d1 == 0)
            return false;

        curve_inc = static_cast<int16_t>(d0 / d1);
        curve_end = static_cast<int16_t>((d2 / d1) * 4);
        return true;
    }

    bool dx_create_curve_exact(
        int32_t curve_x_total,
        int32_t curve_y_total,
        double curve_x_dist,
        double curve_y_dist,
        double& curve_inc)
    {
        const double x = static_cast<double>(curve_x_total) / 32.0;
        const double y = static_cast<double>(curve_y_total) / 32.0;
        const double d0 = x * curve_y_dist - y * curve_x_dist;
        const double d2 = (x * curve_x_dist + y * curve_y_dist) / 128.0;
        const double d1 = d2 / 128.0 + 0x410;

        if (std::abs(d1) < 1.0e-12)
            return false;

        curve_inc = d0 / d1;
        return std::isfinite(curve_inc);
    }

    void dx_pchip_slopes(
        const DxRoadCurveAnchor* anchors,
        int count,
        double* slopes)
    {
        if (count <= 0)
            return;
        if (count == 1)
        {
            slopes[0] = 0.0;
            return;
        }

        double h[DX_HIRES_ROAD_MAX_ANCHORS]{};
        double delta[DX_HIRES_ROAD_MAX_ANCHORS]{};

        for (int i = 0; i < count - 1; ++i)
        {
            h[i] = anchors[i + 1].sample - anchors[i].sample;
            delta[i] = h[i] > 0.0
                ? (anchors[i + 1].value - anchors[i].value) / h[i]
                : 0.0;
        }

        if (count == 2)
        {
            slopes[0] = delta[0];
            slopes[1] = delta[0];
            return;
        }

        for (int i = 1; i < count - 1; ++i)
        {
            const double d0 = delta[i - 1];
            const double d1 = delta[i];

            // Fritsch-Carlson/PCHIP limiter: a direction change gets a zero
            // tangent, preventing the spline from inventing an extra bend.
            if (d0 == 0.0 || d1 == 0.0 || (d0 < 0.0) != (d1 < 0.0))
            {
                slopes[i] = 0.0;
                continue;
            }

            const double w1 = 2.0 * h[i] + h[i - 1];
            const double w2 = h[i] + 2.0 * h[i - 1];
            slopes[i] = (w1 + w2) / (w1 / d0 + w2 / d1);
        }

        auto endpoint_slope = [](double h0, double h1, double d0, double d1)
        {
            if (h0 <= 0.0 || h1 <= 0.0)
                return d0;

            double slope = ((2.0 * h0 + h1) * d0 - h0 * d1) /
                           (h0 + h1);
            if ((slope < 0.0) != (d0 < 0.0))
                return 0.0;
            if ((d0 < 0.0) != (d1 < 0.0) &&
                std::abs(slope) > 3.0 * std::abs(d0))
            {
                slope = 3.0 * d0;
            }
            return slope;
        };

        slopes[0] = endpoint_slope(h[0], h[1], delta[0], delta[1]);
        slopes[count - 1] = endpoint_slope(
            h[count - 2],
            h[count - 3],
            delta[count - 2],
            delta[count - 3]);
    }

    double dx_pchip_sample(
        const DxRoadCurveAnchor* anchors,
        const double* slopes,
        int count,
        double sample)
    {
        if (count <= 0)
            return 0.0;
        if (count == 1 || sample <= anchors[0].sample)
            return anchors[0].value;
        if (sample >= anchors[count - 1].sample)
            return anchors[count - 1].value;

        int segment = 0;
        while (segment + 1 < count - 1 &&
               sample > anchors[segment + 1].sample)
        {
            ++segment;
        }

        const double x0 = anchors[segment].sample;
        const double x1 = anchors[segment + 1].sample;
        const double h = x1 - x0;
        if (h <= 0.0)
            return anchors[segment].value;

        const double t = std::clamp((sample - x0) / h, 0.0, 1.0);
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h01 = -2.0 * t3 + 3.0 * t2;
        const double h11 = t3 - t2;

        return h00 * anchors[segment].value +
               h10 * h * slopes[segment] +
               h01 * anchors[segment + 1].value +
               h11 * h * slopes[segment + 1];
    }

    bool dx_build_hires_road_x_model(
        const ORoad& road,
        std::array<double, DX_HIRES_ROAD_SAMPLES>& output)
    {
        for (int i = 0; i < DX_HIRES_ROAD_SAMPLES; ++i)
            output[i] = static_cast<double>(road.road_x[i]);

        uint32_t addr = road.road_data_offset;
        const int16_t direction_x =
            trackloader.readPath(addr) + trackloader.readPath(addr + 4);
        const int16_t direction_y =
            trackloader.readPath(addr + 2) + trackloader.readPath(addr + 6);

        const int32_t distance_sq =
            static_cast<int32_t>(direction_x) * direction_x +
            static_cast<int32_t>(direction_y) * direction_y;
        const uint16_t distance = outils::isqrt(distance_sq);
        const double distance_exact = std::hypot(
            static_cast<double>(direction_x),
            static_cast<double>(direction_y));

        if (distance == 0 || distance_exact <= 0.0)
            return false;

        const int16_t curve_x_dist = static_cast<int16_t>(
            (static_cast<int32_t>(direction_x) * 16384) / distance);
        const int16_t curve_y_dist = static_cast<int16_t>(
            (static_cast<int32_t>(direction_y) * 16384) / distance);
        const double curve_x_dist_exact =
            static_cast<double>(direction_x) * 16384.0 / distance_exact;
        const double curve_y_dist_exact =
            static_cast<double>(direction_y) * 16384.0 / distance_exact;

        DxRoadCurveAnchor anchors[DX_HIRES_ROAD_MAX_ANCHORS]{};
        int anchor_count = 1;
        anchors[0] = { 0.0, 0.0 };

        int32_t curve_x_total = 0;
        int32_t curve_y_total = 0;
        int16_t curve_start = 0;
        int16_t curve_end = 0;
        int16_t curve_inc = 0;
        int16_t curve_inc_old = 0;
        double curve_inc_exact_old = 0.0;
        int scanline = DX_HIRES_ROAD_FIRST_SAMPLE;
        int emitted_samples = 0;
        bool reached_top = false;

        for (uint8_t i = 0; i <= 0x20 && !reached_top; ++i)
        {
            const int32_t x_next =
                trackloader.readPath(addr) + trackloader.readPath(addr + 4);
            const int32_t y_next =
                trackloader.readPath(addr + 2) + trackloader.readPath(addr + 6);
            addr += 8;

            curve_x_total += x_next;
            curve_y_total += y_next;

            if (!dx_create_curve_integer(
                    curve_x_total,
                    curve_y_total,
                    curve_x_dist,
                    curve_y_dist,
                    curve_inc,
                    curve_end))
            {
                return false;
            }

            double curve_inc_exact = 0.0;
            if (!dx_create_curve_exact(
                    curve_x_total,
                    curve_y_total,
                    curve_x_dist_exact,
                    curve_y_dist_exact,
                    curve_inc_exact))
            {
                return false;
            }

            const int curve_steps = curve_end - curve_start;
            if (curve_steps < 0)
                return false;
            if (curve_steps == 0)
                continue;

            const int32_t xinc =
                (static_cast<int32_t>(curve_inc) - curve_inc_old) /
                curve_steps;
            int32_t original_x = curve_inc_old;
            const int requested = curve_steps + 1;
            int written = 0;

            for (int pos = curve_start; pos <= curve_end; ++pos)
            {
                original_x += xinc;
                if (original_x < -0x3200 || original_x > 0x3200)
                    return false;

                ++written;
                ++emitted_samples;
                if (--scanline < 0)
                {
                    reached_top = true;
                    break;
                }
            }

            if (anchor_count >= DX_HIRES_ROAD_MAX_ANCHORS)
                return false;

            const double completed = requested > 0
                ? static_cast<double>(written) / requested
                : 1.0;
            const double anchor_value = curve_inc_exact_old +
                (curve_inc_exact - curve_inc_exact_old) * completed;
            anchors[anchor_count++] =
                { static_cast<double>(emitted_samples), anchor_value };

            if (!reached_top)
            {
                curve_inc_old = curve_inc;
                curve_inc_exact_old = curve_inc_exact;
                curve_start = curve_end;
            }
        }

        if (anchor_count < 2 || emitted_samples <= 0)
            return false;

        double slopes[DX_HIRES_ROAD_MAX_ANCHORS]{};
        dx_pchip_slopes(anchors, anchor_count, slopes);

        const int max_sample = std::min(emitted_samples, 448);
        for (int sample = 1; sample <= max_sample; ++sample)
        {
            const int index = 448 - sample;
            if (index < 0 || index >= DX_HIRES_ROAD_SAMPLES)
                continue;

            // 0x3210 is a control marker in the original table, not a geometric
            // sample. Keep it untouched so the car-offset ignore rule survives.
            if (road.road_x[index] == 0x3210)
                continue;

            output[index] = std::clamp(
                dx_pchip_sample(
                    anchors,
                    slopes,
                    anchor_count,
                    static_cast<double>(sample)),
                -static_cast<double>(0x3200),
                static_cast<double>(0x3200));
        }

        return true;
    }

    void dx_build_hires_hscroll(
        const ORoad& road,
        const std::array<double, DX_HIRES_ROAD_SAMPLES>& road_x_model,
        double* destination,
        int16_t width,
        bool invert)
    {
        const int32_t car_offset =
            road.car_x_bak + width + oinitengine.camera_x_off;

        for (int i = 0; i < DX_HIRES_ROAD_SAMPLES; ++i)
        {
            const bool ignore_car = road.road_x[i] == 0x3210;
            const double car_component =
                car_offset != 0 && !ignore_car
                    ? static_cast<double>(car_offset) * i / 512.0
                    : 0.0;

            // The marker's numeric value is not a real 6-bit-fraction sample.
            // Preserve the original 0x3210 >> 6 == 200 behaviour exactly.
            const double curve_component = ignore_car
                ? 200.0
                : road_x_model[i] / 64.0;

            destination[i] = car_component +
                (invert ? -curve_component : curve_component);
        }
    }

    void dx_update_hires_road_model(ORoad& road)
    {
        std::array<double, DX_HIRES_ROAD_SAMPLES> road_x_model{};
        if (!dx_build_hires_road_x_model(road, road_x_model))
            return;

        std::array<double, DX_HIRES_ROAD_SAMPLES> road0_h{};
        std::array<double, DX_HIRES_ROAD_SAMPLES> road1_h{};

        for (int i = 0; i < DX_HIRES_ROAD_SAMPLES; ++i)
        {
            road0_h[i] = road.road0_h[i];
            road1_h[i] = road.road1_h[i];
        }

        switch (road.road_ctrl)
        {
            case ORoad::ROAD_R0:
            case ORoad::ROAD_R0_SPLIT:
                dx_build_hires_hscroll(
                    road, road_x_model, road0_h.data(), -road.road_width_bak, false);
                break;

            case ORoad::ROAD_R1:
                dx_build_hires_hscroll(
                    road, road_x_model, road1_h.data(), +road.road_width_bak, false);
                break;

            case ORoad::ROAD_BOTH_P0:
            case ORoad::ROAD_BOTH_P1:
                dx_build_hires_hscroll(
                    road, road_x_model, road0_h.data(), -road.road_width_bak, false);
                dx_build_hires_hscroll(
                    road, road_x_model, road1_h.data(), +road.road_width_bak, false);
                break;

            case ORoad::ROAD_BOTH_P0_INV:
            case ORoad::ROAD_BOTH_P1_INV:
                dx_build_hires_hscroll(
                    road, road_x_model, road0_h.data(), -road.road_width_bak, false);
                dx_build_hires_hscroll(
                    road, road_x_model, road1_h.data(), +road.road_width_bak, true);
                break;

            case ORoad::ROAD_R1_SPLIT:
                dx_build_hires_hscroll(
                    road, road_x_model, road1_h.data(), +road.road_width_bak, true);
                break;

            case ORoad::ROAD_OFF:
            default:
                return;
        }

        hwroad.apply_hires_hscroll_model(road0_h.data(), road1_h.data());
    }
}

void HWRoad::apply_hires_hscroll_model(
    const double* road0_h,
    const double* road1_h)
{
    if (!road0_h || !road1_h)
        return;

    auto apply = [this](int table_base, const double* source)
    {
        for (int i = 0; i < DX_HIRES_ROAD_SAMPLES; ++i)
        {
            const int integer_hpos = ramBuff[table_base + i] & 0x0fff;
            double desired_hpos = 0x654 - source[i];
            desired_hpos = std::fmod(desired_hpos, 4096.0);
            if (desired_hpos < 0.0)
                desired_hpos += 4096.0;

            double delta = desired_hpos - integer_hpos;
            if (delta > 2048.0)
                delta -= 4096.0;
            else if (delta < -2048.0)
                delta += 4096.0;

            // Keep the integer Road RAM untouched so the low-resolution path is
            // still bit-for-bit arcade data. The existing int8 side-buffer gives
            // the model almost +/-2 native pixels of correction (up to +/-8
            // physical pixels at 4x), which is ample for the segment kinks this
            // experiment targets.
            const int fraction = std::clamp(
                static_cast<int>(std::lround(delta * 64.0)),
                -127,
                127);
            ramFracBuff[table_base + i] = static_cast<int8_t>(fraction);
        }
    };

    apply(0x200, road0_h);
    apply(0x400, road1_h);
}

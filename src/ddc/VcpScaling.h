#pragma once

#include <cstdint>

/// Percent <-> raw device-unit conversion for continuous VCP controls.
///
/// The UI operates on a 0-100 percent scale, but a monitor's continuous
/// controls run over 0..max where max comes from the VCP read reply - and
/// max is NOT always 100 (PowerToys PowerDisplay hit Samsung panels
/// reporting max=50; see docs/07_PowerToys_PowerDisplay_Notes.md). Each
/// channel therefore carries the device-reported maximum captured during
/// channel build and converts at the boundaries:
///
///   UI percent --ToRaw--> SetVCPFeature payload
///   VCP read reply  --ToPercent--> cached UI value
///
/// Pure integer math with half-up rounding and no state; unit-tested in
/// tests/VcpScalingTests.cpp. A zero |max| (broken reply) is treated as
/// 100 so no caller can ever divide by zero or collapse a channel's range.
namespace VcpScaling
{
    /// Maps a 0-100 percent value into the device's 0-|max| range. Values
    /// outside 0-100 are clamped here so the write boundary stays total.
    inline uint32_t ToRaw(int percent, uint32_t max)
    {
        const uint32_t range = max == 0 ? 100u : max;
        if (percent <= 0)
        {
            return 0;
        }
        if (percent >= 100)
        {
            return range;
        }
        // round(percent * range / 100), half-up: add half of the DIVISOR.
        return (static_cast<uint32_t>(percent) * range + 50u) / 100u;
    }

    /// Maps a raw device value in 0-|max| back to the UI's 0-100 scale.
    inline int ToPercent(uint32_t raw, uint32_t max)
    {
        const uint32_t range = max == 0 ? 100u : max;
        if (raw >= range)
        {
            return 100;
        }
        // round(raw * 100 / range), half-up: add half of the divisor.
        return static_cast<int>((raw * 100u + range / 2u) / range);
    }
}

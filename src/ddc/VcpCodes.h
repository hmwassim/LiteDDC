#pragma once

#include <windows.h>

// Named VCP (Virtual Control Panel) code constants used by LiteDDC.
// Reference: docs/03_DDC_CI_Protocol_Reference.md.
namespace Vcp
{
    /// Luminance/brightness control (continuous code, read+write).
    inline constexpr BYTE Brightness = 0x10;

    /// Contrast control (continuous code, read+write).
    inline constexpr BYTE Contrast = 0x12;

    /// R/G/B gain controls (continuous codes, read+write). Standard MCCS
    /// color-trim codes; present on most panels that do any DDC at all -
    /// including the reference LG, where the capabilities string does NOT
    /// advertise them yet they answer on the bus (M12 full-sweep evidence).
    inline constexpr BYTE RedGain = 0x16;
    inline constexpr BYTE GreenGain = 0x18;
    inline constexpr BYTE BlueGain = 0x1A;

    /// Saturation axes 0x59-0x5E. There is NO single unified saturation VCP
    /// code in MCCS: vendors implement one or more of these six continuous
    /// codes, so a saturation update fans out to up to six SetVCPFeature
    /// calls (architecture doc section 6). Support per axis must be checked
    /// against each monitor's parsed capability set before writing.
    inline constexpr BYTE SaturationAxes[] = { 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E };

    /// Number of saturation axes (sizeof on the array is a byte count, so expose the count explicitly).
    inline constexpr size_t SaturationAxisCount = sizeof(SaturationAxes) / sizeof(SaturationAxes[0]);

    /// Audio speaker volume (continuous code, read+write). MCCS assigns
    /// 0x62 to VOLUME; on the reference panel it verifiably controls the
    /// monitor's own speakers (M12 nudge test moved it 30->85->10->30,
    /// human heard it - the folklore that LG maps it to saturation is
    /// disproven, docs/03 section 2).
    inline constexpr BYTE Volume = 0x62;
}

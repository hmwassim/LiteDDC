#pragma once

#include <windows.h>

#include <optional>
#include <string>

/// Thin, direct wrappers around Dxva2.dll (linked via Dxva2.lib).
///
/// NULL-HANDLE QUIRK: on at least one test system (2026-08) the OS hands out
/// NULL physical-monitor tokens and these dxva2 functions still succeed,
/// routing to the primary monitor. We pass handles through verbatim - no
/// validation, no fallback - so behavior stays exactly whatever the OS does.
///
/// TIMING - LOAD-BEARING FACT: SetVCPFeature takes about 50 milliseconds to
/// return (Microsoft docs; see docs/03_DDC_CI_Protocol_Reference.md), and a
/// saturation update fans out to up to six such calls. Callers must NEVER
/// invoke these functions on the WndProc/UI thread; all writes belong on
/// AdjustmentCoordinator's dedicated worker thread (architecture doc section 7).
namespace NativeMonitorApi
{
    /// One continuous-VCP read reply: the value plus the range maximum it
    /// was measured against. The max arrives free with every read and is
    /// load-bearing since M10 - panels exist whose brightness max is not
    /// 100 (docs/07), so callers must scale through VcpScaling instead of
    /// assuming the UI's 0-100 equals the device's range.
    struct VcpReading
    {
        uint32_t current = 0;
        uint32_t maximum = 0;
    };

    /// Fetches the monitor's raw MCCS capability string (ANSI, as delivered
    /// by the API). Returns nullopt if the monitor reports no capabilities or
    /// the request fails. This call can take a while on some monitors (I2C
    /// burst read) - treat it as slow like every other DDC call.
    std::optional<std::string> GetCapabilitiesString(HANDLE physicalMonitor);

    /// Reads the current AND maximum value of a continuous VCP code.
    /// Returns nullopt on any failure, including ERROR_UNSUPPORTED_VCP_CODE -
    /// unsupported codes are a normal "control not available" outcome for
    /// this app, never an exception or crash (roadmap non-negotiable).
    std::optional<VcpReading> ReadVcp(HANDLE physicalMonitor, BYTE code);

    /// Writes a new value to a continuous VCP code. Blocks for roughly 50 ms
    /// per call (see note above). Returns false on any failure, including
    /// unsupported codes. When |errorOut| is non-null it receives GetLastError()
    /// on failure (0 on success) so callers can classify via DdcErrors.h.
    bool WriteVcp(HANDLE physicalMonitor, BYTE code, uint32_t value, DWORD* errorOut = nullptr);
}

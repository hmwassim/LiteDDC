#pragma once

#include <windows.h>

/// Win32 DDC error classification (PowerToys PowerDisplay precedent,
/// docs/07 - their Drivers\DDC\DdcErrorClassifier.cs). The dxva2 functions
/// report failures through GetLastError with codes from the graphics
/// facility that the SDK headers do not export, so the constants live here.
///
/// Policy for THIS app:
/// - Terminal: never retried. VCP-not-supported is a capability fact;
///   dead-handle codes mean re-enumeration territory (M6 triggers own
///   that); UNKNOWN codes also land here ON PURPOSE - this machine's
///   read path can wedge mid-session (M4 incident), so repeated pokes at
///   an unclassified failure are exactly what we do not want.
/// - Transient: I2C transaction framing/checksum/timeout style errors -
///   worth exactly ONE immediate retry on the write path.
namespace DdcErrors
{
    enum class Class
    {
        Success,
        Terminal,  ///< give up, no retry
        Transient, ///< retry once is worthwhile
    };

    // Graphics-facility codes not exported by the dxva2 SDK headers.
    constexpr DWORD kDdcciVcpNotSupported = 0xC0262584ul;
    constexpr DWORD kInvalidPhysicalMonitorHandle = 0xC026258Cul;
    constexpr DWORD kMonitorNoLongerExists = 0xC026258Dul;

    // Legacy alias some drivers report instead of the graphics-facility
    // code above (same constant NativeMonitorApi.cpp has always known).
    constexpr DWORD kUnsupportedVcpCodeLegacy = 0xC1FF0000ul;

    // ERROR_TIMEOUT lives in winerror.h (146).

    inline Class Classify(DWORD win32Error)
    {
        if (win32Error == 0)
        {
            return Class::Success;
        }
        switch (win32Error)
        {
        case kDdcciVcpNotSupported:
        case kInvalidPhysicalMonitorHandle:
        case kMonitorNoLongerExists:
        case kUnsupportedVcpCodeLegacy:
            return Class::Terminal;
        case ERROR_TIMEOUT:
        case 0xC0262582ul: // DDCCI_INVALID_MESSAGE_CHECKSUM-style I2C failures
        case 0xC0262583ul:
        case 0xC0262585ul:
        case 0xC0262588ul:
        case 0xC026258Aul:
        case 0xC026258Bul:
            return Class::Transient;
        default:
            return Class::Terminal; // unknown: no retry, by design
        }
    }
}

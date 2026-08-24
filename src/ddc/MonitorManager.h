#pragma once

#include "PhysicalMonitorHandle.h"

#include <windows.h>

#include <set>
#include <string>
#include <vector>

/// Enumerates logical monitors and the physical monitors behind them,
/// caching each physical monitor's supported-VCP-code set (parsed by
/// CapabilitiesParser). Handles the 1-HMONITOR-to-many-physical-monitors
/// case; never assumes 1:1.
///
/// Internal laptop panels are EXCLUDED at enumeration: a QDC target whose
/// outputTechnology is an embedded type (DisplayPort-Embedded / UDI-
/// Embedded) is chassis-internal, so its logical monitor never reaches
/// channel building and can never be poked over DDC. See IsInternalTechnology.
///
/// Hot-plug re-enumeration on WM_DISPLAYCHANGE/WM_DEVICECHANGE arrives in
/// M6 - for now call Refresh() explicitly when monitor topology may have
/// changed. All handles are owned by RAII (PhysicalMonitorHandle), so a
/// Refresh() that disposes stale entries cannot leak.
class MonitorManager
{
public:
    /// One physical panel behind a logical monitor: its RAII handle plus the
    /// capability set claimed by its capability string (possibly empty).
    struct PhysicalEntry
    {
        PhysicalMonitorHandle handle;
        std::set<BYTE> supportedCodes;
    };

    /// One logical monitor (HMONITOR) and everything behind it.
    struct LogicalEntry
    {
        HMONITOR logicalMonitor = nullptr;
        MONITORINFOEXW info{};
        std::vector<PhysicalEntry> physical;

        /// EDID-derived model name from QueryDisplayConfig's
        /// GET_TARGET_NAME, joined to this entry by GDI device name
        /// (PowerToys PowerDisplay precedent, docs/07). Empty when the QDC
        /// query failed or produced nothing usable - callers must fall
        /// back through IsGenericDisplayName-guarded sources themselves.
        std::wstring friendlyName;
    };

    /// Re-enumerates all monitors and rebuilds the cache, disposing stale
    /// RAII handles. Returns true if at least one logical monitor was found
    /// (an empty desktop is treated as failure to enumerate).
    bool Refresh();

    /// The current cache as of the last Refresh(). Do not hold HANDLEs from
    /// it across topology changes; call Refresh() first in that case.
    const std::vector<LogicalEntry>& Monitors() const noexcept;

    /// True when |code| appears in |entry|'s parsed capability set. A code
    /// absent from capabilities must not be written blindly - gate all
    /// reads/writes through this check (or ReadVcp's nullopt result).
    static bool SupportsCode(const PhysicalEntry& entry, BYTE code) noexcept;

    /// True when |name| is null, empty, or a Windows placeholder
    /// ("Generic PnP Monitor" and friends) rather than a real model name.
    /// Both QDC friendly names and driver descriptions can be generic;
    /// guard every display-name source with this before trusting it.
    static bool IsGenericDisplayName(const wchar_t* name) noexcept;

    /// True when a QueryDisplayConfig output technology identifies a panel
    /// BUILT INTO the chassis (INTERNAL, LVDS, DisplayPort-Embedded,
    /// UDI-Embedded) - the eDP/internal family this app cannot and must not
    /// drive over DDC. Everything else (including OTHER/-1) counts as
    /// external so a query gap can never hide a real monitor. Caveat on
    /// record (docs/07, PowerToys issue #48587): a dGPU-driven internal
    /// panel may MISREPORT as DisplayPort-External; that case is accepted -
    /// the bus itself decides what such a panel supports.
    static bool IsInternalTechnology(long outputTechnology) noexcept;

private:
    std::vector<LogicalEntry> m_monitors;
};

#pragma once

#include <windows.h>

#include <vector>

/// Move-only RAII owner of a single physical monitor HANDLE acquired via
/// GetPhysicalMonitorsFromHMONITOR. The destructor releases the handle with
/// DestroyPhysicalMonitors, so handles cannot leak on early returns or
/// exception paths (architecture doc section 8; roadmap non-negotiable).
///
/// Use AcquireForMonitor() to obtain all physical monitors behind one
/// logical HMONITOR - the mapping is 1-to-many, never assume 1:1.
class PhysicalMonitorHandle
{
public:
    /// Acquires every physical monitor behind |hMonitor|. Returns an empty
    /// vector on failure (call GetLastError at the call site if needed).
    static std::vector<PhysicalMonitorHandle> AcquireForMonitor(HMONITOR hMonitor);

    PhysicalMonitorHandle() = default;
    ~PhysicalMonitorHandle();

    PhysicalMonitorHandle(const PhysicalMonitorHandle&) = delete;
    PhysicalMonitorHandle& operator=(const PhysicalMonitorHandle&) = delete;

    PhysicalMonitorHandle(PhysicalMonitorHandle&& other) noexcept;
    PhysicalMonitorHandle& operator=(PhysicalMonitorHandle&& other) noexcept;

    /// True when this object currently owns a non-NULL physical monitor
    /// handle. OBSERVED QUIRK (single-monitor test machine, 2026-08): the OS
    /// can hand out NULL handles with a filled description, and dxva2's
    /// config APIs tolerate NULL - so false here does NOT mean the monitor
    /// is unusable. Never gate reads/writes on this; gate on the capability
    /// set instead (MonitorManager::SupportsCode). Revisit for multi-monitor
    /// safety in M6: on systems mixing real and NULL handles, a NULL write
    /// may target the wrong panel.
    bool IsValid() const noexcept;

    /// The raw handle to pass to Dxva2 API calls. Do not store or free it.
    HANDLE Get() const noexcept;

    /// Monitor description reported by the driver (may be generic, e.g.
    /// "Generic PnP Monitor" - do not treat it as unique per device).
    const wchar_t* Description() const noexcept;

private:
    HANDLE m_handle = nullptr;
    wchar_t m_description[128] = {};
};

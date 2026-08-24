#include "MonitorManager.h"
#include "CapabilitiesParser.h"
#include "NativeMonitorApi.h"

namespace
{
    /// One active display target as reported by QueryDisplayConfig, kept
    /// keyed by its GDI source device name so HMONITOR-based entries can be
    /// matched to EDID-derived names (mirror mode means several targets can
    /// share one source - PowerToys precedent, docs/07).
    struct QdcTarget
    {
        std::wstring gdiSource;
        std::wstring friendlyName;
        long outputTechnology = -1; ///< DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY
    };

    std::vector<QdcTarget> QueryActiveTargets()
    {
        std::vector<QdcTarget> targets;
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS ||
            pathCount == 0)
        {
            return targets;
        }

        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
            ERROR_SUCCESS)
        {
            return targets;
        }

        targets.reserve(pathCount);
        for (UINT32 i = 0; i < pathCount; ++i)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = paths[i].sourceInfo.adapterId;
            source.header.id = paths[i].sourceInfo.id;

            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            target.header.size = sizeof(target);
            target.header.adapterId = paths[i].targetInfo.adapterId;
            target.header.id = paths[i].targetInfo.id;

            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
            {
                continue;
            }

            QdcTarget resolved;
            resolved.gdiSource = source.viewGdiDeviceName;
            resolved.friendlyName = target.monitorFriendlyDeviceName;
            resolved.outputTechnology = static_cast<long>(target.outputTechnology);
            targets.push_back(std::move(resolved));
        }
        return targets;
    }
}

bool MonitorManager::IsGenericDisplayName(const wchar_t* name) noexcept
{
    if (!name || name[0] == L'\0')
    {
        return true;
    }
    // Windows placeholders seen in the wild: "Generic PnP Monitor",
    // "Generic Non-PnP Monitor", "PnP-Monitor (Standard)".
    return wcsstr(name, L"Generic") != nullptr || wcsstr(name, L"PnP") != nullptr;
}

bool MonitorManager::IsInternalTechnology(long outputTechnology) noexcept
{
    // Only technologies that identify a chassis-internal panel: the
    // explicit INTERNAL flag, the classic LVDS laptop connector, and the
    // embedded DisplayPort/UDI variants. Everything else - including OTHER
    // (-1) - counts as external so a missing/misreported field can never
    // hide a real monitor (docs/07: PowerToys issue #48587 showed a dGPU-
    // driven internal panel misreporting as DisplayPort-External; such
    // panels stay listed here and the bus decides what they support).
    return outputTechnology == static_cast<long>(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL) ||
           outputTechnology == static_cast<long>(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS) ||
           outputTechnology == static_cast<long>(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED) ||
           outputTechnology == static_cast<long>(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED);
}

bool MonitorManager::Refresh()
{
    // QDC targets are resolved FIRST, not last: besides EDID-friendly names
    // they carry each path's output technology, which gates internal laptop
    // panels out BEFORE any physical handle is acquired or capability string
    // read - an excluded panel never gets so much as one DDC poke.
    const std::vector<QdcTarget> targets = QueryActiveTargets();

    std::vector<HMONITOR> logicalMonitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(lParam)->push_back(hMonitor);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&logicalMonitors)))
    {
        return false;
    }

    std::vector<LogicalEntry> refreshed;
    refreshed.reserve(logicalMonitors.size());

    for (const HMONITOR hMonitor : logicalMonitors)
    {
        LogicalEntry entry;
        entry.logicalMonitor = hMonitor;
        entry.info.cbSize = sizeof(entry.info);
        if (!GetMonitorInfoW(hMonitor, &entry.info))
        {
            continue;
        }

        // Join this logical monitor to its QDC target(s) once, by GDI device
        // name; BOTH the internal-panel gate and the friendly-name choice
        // below consume the same candidate match (mirror mode can share one
        // GDI source across several targets).
        const QdcTarget* chosen = nullptr;
        bool anyCandidate = false;
        bool allCandidatesInternal = true;
        for (const QdcTarget& candidate : targets)
        {
            if (_wcsicmp(candidate.gdiSource.c_str(), entry.info.szDevice) != 0)
            {
                continue;
            }
            anyCandidate = true;
            if (!IsInternalTechnology(candidate.outputTechnology))
            {
                allCandidatesInternal = false;
            }
            if (!IsGenericDisplayName(candidate.friendlyName.c_str()))
            {
                chosen = &candidate;
                break;
            }
            if (!chosen)
            {
                chosen = &candidate;
            }
        }

        // Internal-panel gate: skip only when topology data exists AND every
        // target behind this source reports an embedded panel (a clone of
        // internal+external stays listed). Zero candidates - QDC failure -
        // keeps the monitor listed: trust-the-bus, same philosophy as the
        // M12 channel detection.
        if (anyCandidate && allCandidatesInternal)
        {
            continue;
        }

        for (PhysicalMonitorHandle& handle : PhysicalMonitorHandle::AcquireForMonitor(hMonitor))
        {
            PhysicalEntry physical;
            physical.handle = std::move(handle);
            if (const auto capabilities = NativeMonitorApi::GetCapabilitiesString(physical.handle.Get()))
            {
                physical.supportedCodes = CapabilitiesParser::ParseSupportedVcpCodes(*capabilities);
            }
            // A monitor with no/unreadable capability string stays listed
            // with an empty code set: it is visible but gated off everywhere.
            entry.physical.push_back(std::move(physical));
        }

        if (chosen)
        {
            entry.friendlyName = chosen->friendlyName;
        }

        refreshed.push_back(std::move(entry));
    }

    // Replacing the vector destroys the old entries' RAII handles in order.
    m_monitors = std::move(refreshed);
    return !m_monitors.empty();
}

const std::vector<MonitorManager::LogicalEntry>& MonitorManager::Monitors() const noexcept
{
    return m_monitors;
}

bool MonitorManager::SupportsCode(const PhysicalEntry& entry, BYTE code) noexcept
{
    return entry.supportedCodes.find(code) != entry.supportedCodes.end();
}

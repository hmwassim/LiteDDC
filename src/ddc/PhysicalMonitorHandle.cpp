#include "PhysicalMonitorHandle.h"
#include <physicalmonitorenumerationapi.h>

#include <utility>

std::vector<PhysicalMonitorHandle> PhysicalMonitorHandle::AcquireForMonitor(HMONITOR hMonitor)
{
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &count) || count == 0)
    {
        return {};
    }

    std::vector<PHYSICAL_MONITOR> raw(count);
    if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, count, raw.data()))
    {
        // Nothing was handed out on failure, but be defensive: destroy whatever
        // the array holds in case a partial fill left valid handles behind.
        DestroyPhysicalMonitors(count, raw.data());
        return {};
    }

    std::vector<PhysicalMonitorHandle> result;
    result.reserve(count);
    for (DWORD i = 0; i < count; ++i)
    {
        PhysicalMonitorHandle handle;
        handle.m_handle = raw[i].hPhysicalMonitor;
        wcsncpy_s(handle.m_description, raw[i].szPhysicalMonitorDescription, _TRUNCATE);
        raw[i].hPhysicalMonitor = nullptr; // ownership transferred to the RAII object
        result.push_back(std::move(handle));
    }

    // All handles were transferred into RAII objects above; nothing to free here.
    return result;
}

PhysicalMonitorHandle::~PhysicalMonitorHandle()
{
    if (m_handle)
    {
        PHYSICAL_MONITOR monitor{};
        monitor.hPhysicalMonitor = m_handle;
        DestroyPhysicalMonitors(1, &monitor);
        m_handle = nullptr;
    }
}

PhysicalMonitorHandle::PhysicalMonitorHandle(PhysicalMonitorHandle&& other) noexcept :
    m_handle(other.m_handle)
{
    wcscpy_s(m_description, other.m_description);
    other.m_handle = nullptr;
    other.m_description[0] = L'\0';
}

PhysicalMonitorHandle& PhysicalMonitorHandle::operator=(PhysicalMonitorHandle&& other) noexcept
{
    if (this != &other)
    {
        if (m_handle)
        {
            PHYSICAL_MONITOR monitor{};
            monitor.hPhysicalMonitor = m_handle;
            DestroyPhysicalMonitors(1, &monitor);
        }
        m_handle = other.m_handle;
        wcscpy_s(m_description, other.m_description);
        other.m_handle = nullptr;
        other.m_description[0] = L'\0';
    }
    return *this;
}

bool PhysicalMonitorHandle::IsValid() const noexcept
{
    return m_handle != nullptr;
}

HANDLE PhysicalMonitorHandle::Get() const noexcept
{
    return m_handle;
}

const wchar_t* PhysicalMonitorHandle::Description() const noexcept
{
    return m_description;
}

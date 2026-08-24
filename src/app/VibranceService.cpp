#include "VibranceService.h"

#include <cstdint>

namespace
{
    // Interface ids for the nvapi function pointers resolved below. Values
    // cross-checked against the community-maintained id tables
    // (jNizM/AHK_NVIDIA_NvAPI info/NvAPI_IDs) after the M13 probe proved
    // them against the live driver.
    constexpr unsigned kIdInitialize = 0x0150E828;
    constexpr unsigned kIdUnload = 0xD22BDD7E;
    constexpr unsigned kIdEnumDisplayHandles = 0x9ABDD40D;
    constexpr unsigned kIdGetDisplayName = 0x22A78B05;
    constexpr unsigned kIdGetDvcInfoEx = 0x0E45002D;
    constexpr unsigned kIdSetDvcLevelEx = 0x4A82C2B1;

    // Sentinel the SEH guards return when a driver call faults. Any
    // nonzero status reads as "call failed" to nvapi callers.
    constexpr int kCallFault = -1;
}

VibranceService::~VibranceService()
{
    Release();
}

void VibranceService::TryLoad()
{
    if (m_library)
    {
        return;
    }

    // 64-bit processes must load nvapi64.dll; "nvapi.dll" is its 32-bit twin
    // (only present under SysWOW64). On ARM64 both names either miss or are
    // x64 images that cannot map into this process - LoadLibrary fails and
    // Available() stays false, which is exactly the graceful-absence story.
    m_library = LoadLibraryW(L"nvapi64.dll");
    if (!m_library)
    {
        return;
    }

    const auto queryInterface =
        reinterpret_cast<QueryInterfaceFn>(GetProcAddress(m_library, "nvapi_QueryInterface"));
    if (!queryInterface)
    {
        Release();
        return;
    }

    if (!ProbeDriver(queryInterface) || !m_available)
    {
        // Either the driver FAULTED inside an undocumented interface
        // (struct-version/ABI mismatch on this machine's driver, most
        // likely), or the probe completed without finding a usable NVIDIA
        // display - same outcome as before the SEH hardening: drop
        // everything so Available() stays false and the dialog grays the
        // vibrance row. Never propagate a crash from here.
        Release();
    }
}

bool VibranceService::ProbeDriver(QueryInterfaceFn queryInterface)
{
    // Everything in here is POD (pointers + one wchar_t buffer), which is
    // what makes __try legal alongside it. Any access violation anywhere in
    // the reverse-engineered call chain unwinds via __except instead of
    // killing the process.
    __try
    {
        const auto initialize =
            reinterpret_cast<InitializeFn>(queryInterface(kIdInitialize));
        m_unload = reinterpret_cast<UnloadFn>(queryInterface(kIdUnload));
        m_enumDisplayHandles =
            reinterpret_cast<EnumDisplayHandlesFn>(queryInterface(kIdEnumDisplayHandles));
        const auto getDisplayName =
            reinterpret_cast<GetDisplayNameFn>(queryInterface(kIdGetDisplayName));
        m_getDvcInfo = reinterpret_cast<GetDvcInfoExFn>(queryInterface(kIdGetDvcInfoEx));
        m_setDvcLevel = reinterpret_cast<SetDvcLevelExFn>(queryInterface(kIdSetDvcLevelEx));

        if (!initialize || !m_unload || !m_enumDisplayHandles || !getDisplayName ||
            !m_getDvcInfo || !m_setDvcLevel)
        {
            return true; // resolved nothing usable: graceful unavailable
        }
        if (initialize() != 0)
        {
            return true; // driver refused init: graceful unavailable
        }

        // Claim the first NVIDIA display that answers a DVC query. outputId
        // 0 is what the probe validated on this machine.
        for (unsigned index = 0;; ++index)
        {
            HANDLE display = nullptr;
            if (m_enumDisplayHandles(index, &display) != 0 || !display)
            {
                break;
            }
            wchar_t name[64];
            if (getDisplayName(display, name, 64) != 0)
            {
                continue;
            }
            DvcInfoEx info{};
            info.version = kDvcInfoVersion;
            if (m_getDvcInfo(display, 0, &info) == 0)
            {
                m_display = display;
                m_min = info.min;
                m_max = info.max;
                m_available = true;
                break;
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool VibranceService::GuardedGetDvc(DvcInfoEx* info, int* statusOut)
{
    __try
    {
        *statusOut = m_getDvcInfo(m_display, 0, info);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool VibranceService::GuardedSetDvc(DvcInfoEx info)
{
    __try
    {
        // By-value parameter: the nvapi signature takes a non-const pointer,
        // and the driver may write the reply fields back. The nvapi status
        // is deliberately discarded - set failures are silent no-ops by
        // design (same philosophy as unknown-DDC errors).
        m_setDvcLevel(m_display, 0, &info);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

int VibranceService::GuardedUnload()
{
    __try
    {
        return m_unload();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return kCallFault;
    }
}

int VibranceService::GetLevel()
{
    if (!m_available || !m_getDvcInfo)
    {
        return -1;
    }
    DvcInfoEx info{};
    info.version = kDvcInfoVersion;
    int status = 0;
    if (!GuardedGetDvc(&info, &status))
    {
        // A fault means the contract validated on one machine/driver just
        // broke here: retire the interface so the row degrades to
        // "unavailable" instead of faulting on every refresh.
        Release();
        return -1;
    }
    if (status != 0)
    {
        return -1; // ordinary read failure (e.g. display asleep): stay available
    }
    return info.current;
}

void VibranceService::SetLevel(int level)
{
    if (!m_available || !m_setDvcLevel)
    {
        return;
    }
    DvcInfoEx info{};
    info.version = kDvcInfoVersion;
    info.current = level < m_min ? m_min : (level > m_max ? m_max : level);
    info.min = m_min;
    info.max = m_max;
    // default field mirrors the query reply's neutral level semantics; the
    // driver ignores it here but the probe only validated fully-filled
    // structs, so keep the layout honest.
    info.def = info.current;
    if (!GuardedSetDvc(info))
    {
        // Fault-only degradation (see GetLevel); ordinary write failures
        // stay silent no-ops - never retry, never nag.
        Release();
    }
}

void VibranceService::Release()
{
    if (m_unload)
    {
        GuardedUnload(); // unload is an nvapi call too: guard it identically
        m_unload = nullptr;
    }
    if (m_library)
    {
        FreeLibrary(m_library);
        m_library = nullptr;
    }
    m_display = nullptr;
    m_enumDisplayHandles = nullptr;
    m_getDvcInfo = nullptr;
    m_setDvcLevel = nullptr;
    m_available = false;
}

#pragma once

#include <windows.h>

#include <cstdint>

/// M13: NVIDIA Digital Vibrance, driven through a DYNAMICALLY LOADED NvAPI
/// (LoadLibrary "nvapi64.dll" + the single nvapi_QueryInterface export -
/// zero import-lib dependencies, so the NFR5 no-bundled-binaries rule and
/// clean ARM64 builds both hold). This is what ClickMonitorDDC's
/// "saturation" actually is (vendor readme: command k = "sets
/// color-saturation/vibration in graphics driver"); this monitor exposes NO
/// DDC saturation codes (docs/03 section 2 sweep evidence), so GPU vibrance
/// is the only saturation-like control it can ever have.
///
/// DELIBERATE PIPELINE DEVIATION (roadmap M13): vibrance does NOT go through
/// AdjustmentCoordinator. The coordinator's worker thread exists because DDC
/// writes are slow (~50 ms I2C per code); NvAPI DVC calls are quick
/// driver-side ioctls - the M13 probe measured get+set+set+set+get as
/// effectively instant - and they address the GPU, not a monitor handle, so
/// hot-plug/scope/re-enumeration bookkeeping would be dead weight here.
///
/// Threading contract: UI thread only (dialog handlers / App::Init). No
/// internal locking for the same reason.
///
/// CRASH POLICY (SEH): every resolved nvapi pointer call runs inside a
/// __try/__except wrapper. This interface is undocumented, reverse-
/// engineered, and its struct-version contract was validated on exactly
/// ONE machine/driver combination (RTX 3060); a driver mismatch elsewhere
/// must degrade to "unavailable" (row grays out), never access-violate
/// the whole app. The guarded functions deliberately contain no locals
/// with destructors so SEH use is legal there.
///
/// Scale note (probe evidence on RTX 3060): GetDVCInfoEx reports min=0,
/// max=100, default=50 - the SAME 0-100 percent scale every DDC row uses,
/// with 50 as neutral, so no VcpScaling conversion is needed.
class VibranceService
{
public:
    VibranceService() = default;
    ~VibranceService();

    VibranceService(const VibranceService&) = delete;
    VibranceService& operator=(const VibranceService&) = delete;

    /// Loads the driver interface once. Cheap and safe to call repeatedly;
    /// result is sticky for the process lifetime (a mid-session GPU arrival
    /// is not worth re-probing complexity). UI thread only.
    void TryLoad();

    /// True when an NVIDIA display accepted the DVC query at load time.
    /// On machines without NVIDIA hardware - or under ARM64, where the x64
    /// nvapi64.dll image cannot load into this process - this stays false
    /// and the dialog grays the row out.
    bool Available() const { return m_available; }

    /// Live current level (0-100), or -1 if unavailable / read failed.
    int GetLevel();

    /// Applies |level| (clamped to the device-reported range). Silent no-op
    /// while unavailable; set failures are swallowed by design (same
    /// philosophy as unknown-DDC-error handling: never retry, never nag).
    void SetLevel(int level);

    /// Device-reported DVC range captured at load (probe evidence: 0-100).
    int MinLevel() const { return m_min; }
    int MaxLevel() const { return m_max; }

private:
    // NV_DISPLAY_DVC_INFO_EX: 20 bytes - version + current/min/max/default.
    struct DvcInfoEx
    {
        uint32_t version;
        int32_t current;
        int32_t min;
        int32_t max;
        int32_t def;
    };

    // MAKE_NVAPI_VERSION(NV_DISPLAY_DVC_INFO_EX, 1). Getting this wrong
    // yields INCOMPATIBLE_STRUCT_VERSION (-9) from every call - learned the
    // hard way in the M13 probe when the defaultLevel field was missing.
    static constexpr uint32_t kDvcInfoVersion = (1u << 16) | 20u;

    // nvapi exposes ONE export; everything else is resolved by interface
    // ids through it (values cross-checked against the community id tables
    // after the M13 probe proved them against the live driver).
    using QueryInterfaceFn = void* (__cdecl*)(unsigned id);
    using InitializeFn = int(__cdecl*)();
    using UnloadFn = int(__cdecl*)();
    using EnumDisplayHandlesFn = int(__cdecl*)(unsigned index, HANDLE* handle);
    using GetDisplayNameFn = int(__cdecl*)(HANDLE handle, wchar_t* name, unsigned len);
    using GetDvcInfoExFn = int(__cdecl*)(HANDLE handle, unsigned outputId, DvcInfoEx* info);
    using SetDvcLevelExFn = int(__cdecl*)(HANDLE handle, unsigned outputId, DvcInfoEx* info);

    // SEH guards - see the crash-policy note in the class comment. Each
    // returns a failure signal when the driver call faults, so callers
    // degrade to "unavailable" instead of dying.
    /// Resolves the remaining pointers, initializes nvapi, and claims the
    /// first display that answers a DVC query (fills m_* members). Returns
    /// false only on a hardware-level fault; a plain "no NVIDIA display"
    /// outcome is a graceful path that just leaves m_available false.
    bool ProbeDriver(QueryInterfaceFn queryInterface);
    /// True when the DVC read COMPLETED (|statusOut| then holds its nvapi
    /// status, zero = success); false only when the call faulted.
    bool GuardedGetDvc(DvcInfoEx* info, int* statusOut);
    /// True when the write call completed without faulting (its nvapi
    /// status is deliberately discarded - set failures are silent by
    /// design); false only on a fault.
    bool GuardedSetDvc(DvcInfoEx info);
    int GuardedUnload();

    void Release();

    HMODULE m_library = nullptr;
    UnloadFn m_unload = nullptr;
    EnumDisplayHandlesFn m_enumDisplayHandles = nullptr;
    GetDvcInfoExFn m_getDvcInfo = nullptr;
    SetDvcLevelExFn m_setDvcLevel = nullptr;

    /// First NVIDIA display that answered the DVC query (probe: exactly one
    /// per machine in practice). Multi-GPU/multi-display nuance deferred -
    /// documented simplification, matches the single-monitor reality.
    HANDLE m_display = nullptr;
    int m_min = 0;
    int m_max = 100;
    bool m_available = false;
};

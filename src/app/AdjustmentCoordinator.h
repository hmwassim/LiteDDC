#pragma once

#include "VcpBus.h"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

class MonitorManager;

/// The seven adjustable parameters LiteDDC knows. Order is load-bearing
/// only within this file's channel bookkeeping; UI code addresses them by
/// this enum exclusively.
enum class Param
{
    Brightness = 0,
    Contrast = 1,
    Saturation = 2,
    RedGain = 3,
    GreenGain = 4,
    BlueGain = 5,
    Volume = 6, ///< monitor speaker volume (0x62), M14
};

constexpr int kParamCount = 7;

/// Coalesces rapid input (tray-scroll notches, SettingsDialog slider drags -
/// same path per roadmap) into ONE DDC/CI write per code per monitor per
/// debounce window, executed on a dedicated worker thread.
///
/// Non-negotiables honored here (architecture doc section 7):
/// - SetVCPFeature blocks ~50 ms, so writes NEVER run on the UI/message
///   thread; the UI thread only updates integer targets under a mutex.
/// - Last value wins: intermediate targets are REPLACED, not queued.
/// - Saturation fans out to every axis code the capability set claims in
///   one batch; one failing/unsupported axis never aborts the others.
///
/// M10 value scaling: everything cached/exposed above (Channel::current,
/// AdjustAll/AdjustTo targets, GetValue results) is on a 0-100 PERCENT
/// scale. Each channel stores its device-reported raw maximum (captured
/// from the VCP read reply at channel build - panels exist where max!=100,
/// docs/07) and converts to/from raw units at the hardware boundary via
/// VcpScaling, so mixed-maximum monitor sets stay consistent per channel.
///
/// Current values live in this object (seeded by real reads on the worker
/// thread during its first pass - see Start()), so the hot path never
/// performs DDC reads - those are as slow as writes. Handles are borrowed
/// from MonitorManager's RAII entries and stay valid because M6 hot-plug
/// re-enumeration is the only thing that ever disposes them, and it runs on
/// THIS worker thread (RequestReenumerate): MonitorManager::Refresh()
/// replaces its vector only while no one else can touch it, and the fresh
/// channel set swaps in atomically under m_mutex - query APIs always see a
/// complete old or complete new set, never a half-built one.
///
/// M4 state owned here (all mutex-guarded, all in-memory until M5
/// persists it): which parameters the SCROLL gesture affects (checkboxes),
/// which monitor(s) are targeted (scope), and the scroll step size. The
/// sliders deliberately bypass the enable flags - per UI spec section 3 a
/// checkbox gates scrolling only, never the slider.
class AdjustmentCoordinator
{
public:
    /// Default value added per scroll notch on the 0-100 scale. M4 makes
    /// the step user-selectable (SetScrollStep); this stays the default.
    static constexpr int kDefaultScrollStep = 2;

    /// Quiet time that must elapse after the latest input before pending
    /// targets flush to hardware. Fresh input extends the window, so a
    /// fast scroll burst produces exactly one write at the final value.
    static constexpr int kDebounceWindowMs = 100;

    /// Sentinel results for GetValue().
    static constexpr int kValueUnsupported = -1; ///< no matching channel exists
    static constexpr int kValueMixed = -2;       ///< all-scope channels disagree

    AdjustmentCoordinator() = default;
    ~AdjustmentCoordinator();

    AdjustmentCoordinator(const AdjustmentCoordinator&) = delete;
    AdjustmentCoordinator& operator=(const AdjustmentCoordinator&) = delete;

    /// Spawns the worker thread and returns almost immediately. ALL monitor
    /// I/O - enumeration, capability parsing, and the baseline VCP reads
    /// used to seed each channel's current value - runs on that worker
    /// thread, never on the caller's. This is load-bearing, not tidiness:
    /// App installs a process-wide WH_MOUSE_LL hook, and a low-level hook's
    /// owning thread must keep pumping messages or Windows stalls mouse/
    /// keyboard input SYSTEM-WIDE (every app, not just this one) until that
    /// thread is responsive again. An earlier version of this code ran
    /// enumeration + baseline reads synchronously here, on the UI thread,
    /// after the hook was already installed - that froze the whole desktop
    /// for as long as monitor I/O took. Do not reintroduce that.
    ///
    /// |bus| is the VCP transport seam (VcpBus.h); pass nullptr for the
    /// production NativeVcpBus. Repeated calls are ignored until Stop().
    /// A scroll that arrives before the worker finishes its first
    /// enumeration is safely dropped, not queued or crashed on (see
    /// m_ready in AdjustAll()).
    void Start(MonitorManager* monitors, IVcpBus* bus = nullptr);

    /// Signals the worker and joins it, dropping un-flushed targets.
    /// Safe to call repeatedly or without a prior Start().
    void Stop();

    /// M6 hot-plug / sleep-wake entry point (UI thread safe, non-blocking):
    /// asks the worker to re-run MonitorManager::Refresh() + channel
    /// building, then swap the fresh set in under the mutex. Deliberately
    /// does NOT kill/restart the thread - Stop() would join mid-DDC-I/O on
    /// the UI thread, and a second thread touching the same handles would
    /// race the first. Un-flushed pending targets are dropped: after a
    /// topology change stale targets may address dead handles.
    void RequestReenumerate();

    /// Moves every ENABLED parameter by |steps| * ScrollStep(), clamped to
    /// 0-100, across the current monitor scope. UI thread only; returns
    /// immediately - hardware writes happen later on the worker thread.
    /// A no-op while the worker's first enumeration is still in flight
    /// (see m_ready).
    void AdjustAll(int steps);

    /// Sets |param| to the absolute |value| (0-100, clamped) across the
    /// current monitor scope through the SAME debounce path as scrolling.
    /// This is the slider entry point: it deliberately ignores the scroll
    /// enable checkboxes (UI spec section 3 - they gate scrolling only).
    /// UI thread only.
    void AdjustTo(Param param, int value);

    // --- M4 in-memory settings (no persistence until M5) ---

    /// Monitor scope for BOTH scroll gestures and sliders: -1 = all
    /// monitors, otherwise an index into MonitorManager::Monitors() order
    /// (as built during the worker's enumeration).
    void SetScope(int monitorIndex);
    int Scope() const;

    /// Whether the tray SCROLL gesture affects |param|. Sliders ignore
    /// this. Brightness/contrast/saturation default to enabled and are the
    /// only scrollable params; R/G/B gains stay permanently disabled here -
    /// they are slider-only rows with no checkbox (M12 human decision:
    /// scrolling must never shift color balance).
    void SetParamEnabled(Param param, bool enabled);
    bool IsParamEnabled(Param param) const;

    /// Value added per scroll notch (0-100 scale). Clamped to >= 1.
    void SetScrollStep(int step);
    int ScrollStep() const;

    // --- Read-only queries for the Settings dialog ---

    /// True when |param| has at least one writable channel inside the
    /// given scope (-1 = all monitors). Backs the row-graying rule: an
    /// unsupported parameter is grayed out with text, never hidden.
    bool SupportsParam(Param param, int monitorIndex) const;

    /// Current cached value of |param| within the given scope (-1 = all).
    /// Returns kValueUnsupported when nothing matches, kValueMixed when
    /// all-scope channels disagree (dialog shows a neutral slider then).
    int GetValue(Param param, int monitorIndex) const;

    /// Fired from the worker thread after EVERY enumeration cycle - the
    /// first one after Start(), and again after each RequestReenumerate()
    /// (hot-plug, sleep/wake) completes. True if at least one adjustable
    /// channel was found in that pass. Do not call UI/shell APIs directly
    /// from inside this callback; marshal back to the UI thread (e.g.
    /// PostMessage) as App does.
    void SetOnReady(std::function<void(bool)> cb) { m_onReady = std::move(cb); }

    /// True once the worker's first enumeration has finished - channels are
    /// populated or proven absent. Query APIs distinguish real capability
    /// facts from "detection still running"; the settings dialog uses this
    /// to show "detecting..." instead of wrongly graying rows as unsupported.
    bool Ready() const;

    // --- Test seam (pre-release review item 8) ---

    /// Synthetic channel description for LoadChannelsForTest. |monitor| is
    /// an opaque token the fake bus keys its expectations on; production
    /// code never constructs one of these.
    struct ChannelSeed
    {
        Param param = Param::Brightness;
        HANDLE monitor = nullptr;
        int monitorIndex = 0;
        std::vector<BYTE> codes;
        int current = 0;
        uint32_t maxValue = 100;
    };

    /// TEST-ONLY entry point (tests/CoordinatorTests.cpp): installs a
    /// synthetic channel set and starts the worker against |bus| WITHOUT
    /// priming re-enumeration, so no MonitorManager or hardware is ever
    /// touched. Production code must never call this. Repeated calls and
    /// calls after Start() are ignored until Stop().
    void LoadChannelsForTest(const std::vector<ChannelSeed>& seeds, IVcpBus* bus);

private:
    /// One adjustable logical parameter behind one physical monitor.
    /// Brightness/Contrast/R-G-B gains carry a single code; Saturation
    /// carries every supported axis code (all written to the same target).
    /// NOTE: the flush loop reads monitor/codes/maxValue WITHOUT the mutex
    /// while AdjustAll may concurrently mutate current - distinct members,
    /// i.e. distinct memory locations, so no data race; maxValue is written
    /// only during BuildFreshChannels, before the vector is swapped in, so
    /// it is immutable once observable. The whole VECTOR is only ever
    /// replaced (M6 re-enumeration) by this worker thread itself, under the
    /// mutex, between write batches.
    struct Channel
    {
        HANDLE monitor = nullptr;
        std::vector<BYTE> codes;
        int current = 0;              ///< UI percent scale (0-100)
        uint32_t maxValue = 100;      ///< device-reported raw range max (M10)
        Param param = Param::Brightness;
        int monitorIndex = 0; ///< index into MonitorManager::Monitors()
    };

    /// Builds a fresh channel set from |monitors|' CURRENT enumeration and
    /// returns it; the caller swaps it in under the mutex. Never touches
    /// m_channels directly, so it is safe to run unlocked mid-loop.
    std::vector<Channel> BuildFreshChannels(MonitorManager* monitors);
    void WorkerLoop();

    NativeVcpBus m_nativeBus;   ///< default transport when no bus is injected
    IVcpBus* m_bus = &m_nativeBus;
    /// Borrowed by Start(); the worker only dereferences it inside the
    /// re-enumeration branch (never taken on the test-seam path).
    MonitorManager* m_monitors = nullptr;

    std::vector<Channel> m_channels;
    std::unordered_map<size_t, int> m_pending;

    std::thread m_worker;
    mutable std::mutex m_mutex; // const query APIs lock it too
    std::condition_variable m_cv;
    std::chrono::steady_clock::time_point m_lastInputTime{};
    bool m_started = false;
    bool m_stopping = true;
    /// Set by RequestReenumerate() (and by Start(), as the first job);
    /// consumed by the worker loop. Under m_mutex.
    bool m_reenumerateRequested = false;
    /// True once the worker's first BuildChannels() call has completed
    /// (whether or not it found any channels). AdjustAll() must not touch
    /// m_channels before this is true - the worker may still be mid-
    /// enumeration on a real machine for a few hundred ms after Start().
    bool m_ready = false;
    std::function<void(bool)> m_onReady;

    // In-memory settings (UI thread writers, guarded by m_mutex like the
    // hot path so the worker's flush loop can never observe torn state).
    int m_scope = -1; ///< -1 = all monitors
    /// Volume defaults to scroll-DISABLED like the color rows: a wheel
    /// notch silently changing loudness is more surprise than convenience
    /// (M14); the checkbox lets users opt in.
    bool m_enabled[kParamCount] = { true, true, true, false, false, false, false };
    int m_scrollStep = kDefaultScrollStep;
};

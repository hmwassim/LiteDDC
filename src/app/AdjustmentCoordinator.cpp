#include "AdjustmentCoordinator.h"

#include "../ddc/DdcErrors.h"
#include "../ddc/MonitorManager.h"
#include "../ddc/VcpCodes.h"
#include "../ddc/VcpScaling.h"

namespace
{
    // The UI/cache scale is PERCENT (0-100) since M10; the raw device range
    // is per-channel (Channel::maxValue, from the VCP read reply). Every
    // panel seen so far reports max=100 for brightness/contrast/saturation
    // (M0/M1 checkpoint fact), so on this machine the scaling is an
    // identity - it exists for the panels that do not (docs/07).
    constexpr int kMinValue = 0;
    constexpr int kMaxValue = 100;

    int Clamp(int value)
    {
        if (value < kMinValue)
        {
            return kMinValue;
        }
        if (value > kMaxValue)
        {
            return kMaxValue;
        }
        return value;
    }

    // One immediate retry for transient I2C-style write failures. Kept well
    // under one SetVCPFeature duration so a retried batch stays bounded.
    constexpr DWORD kWriteRetryDelayMs = 40;

    // Scope test for an EXPLICIT scope value (-1 = all monitors), applied to
    // one channel's monitor index. Query APIs use this with their argument;
    // the hot path passes m_scope.
    bool InExplicitScope(int channelMonitorIndex, int scope)
    {
        return scope < 0 || channelMonitorIndex == scope;
    }
}

AdjustmentCoordinator::~AdjustmentCoordinator()
{
    Stop();
}

void AdjustmentCoordinator::Start(MonitorManager* monitors, IVcpBus* bus)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_started || !m_stopping)
        {
            return;
        }
        if (!monitors)
        {
            return;
        }
        m_monitors = monitors;
        m_bus = bus ? bus : &m_nativeBus;
        m_started = true;
        m_stopping = false;
        m_ready = false;
        m_reenumerateRequested = true;
    }

    // Spawns and returns immediately. See the header comment on Start():
    // no monitor I/O may happen on the calling thread, because by the time
    // App calls this it has already (or is about to have) installed a
    // process-wide WH_MOUSE_LL hook whose owning thread must stay
    // responsive.
    m_worker = std::thread(&AdjustmentCoordinator::WorkerLoop, this);
}

void AdjustmentCoordinator::LoadChannelsForTest(const std::vector<ChannelSeed>& seeds, IVcpBus* bus)
{
    std::vector<Channel> channels;
    channels.reserve(seeds.size());
    for (const ChannelSeed& seed : seeds)
    {
        Channel ch;
        ch.param = seed.param;
        ch.monitor = seed.monitor;
        ch.monitorIndex = seed.monitorIndex;
        ch.codes = seed.codes;
        ch.current = Clamp(seed.current);
        ch.maxValue = seed.maxValue == 0 ? 100u : seed.maxValue;
        channels.push_back(std::move(ch));
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started || !m_stopping)
    {
        return;
    }
    m_channels = std::move(channels);
    m_pending.clear();
    m_bus = bus ? bus : &m_nativeBus;
    m_started = true;
    m_stopping = false;
    m_ready = true;
    // Deliberately NOT priming m_reenumerateRequested: the synthetic set
    // stays put and no MonitorManager is touched.
    m_worker = std::thread(&AdjustmentCoordinator::WorkerLoop, this);
}

void AdjustmentCoordinator::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started)
        {
            return;
        }
        m_started = false;
        m_stopping = true;
        m_pending.clear();
    }
    m_cv.notify_all();

    // If the worker is still inside monitors->Refresh()/BuildChannels() (the
    // first few hundred ms after Start()), notify_all() can't interrupt a
    // blocking Win32 call - join() just waits for it to finish naturally.
    // That window is short and bounded, unlike the bug this replaced.
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

void AdjustmentCoordinator::AdjustAll(int steps)
{
    if (steps == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started || m_stopping || !m_ready)
    {
        // Not running, shutting down, or the worker's first enumeration
        // hasn't finished yet - m_channels may not exist or be mid-build.
        // Dropping a scroll that arrives in that narrow startup window is
        // safe and intentional; see Start()'s header comment.
        return;
    }

    const int delta = steps * m_scrollStep;
    bool anyPending = false;
    for (size_t i = 0; i < m_channels.size(); ++i)
    {
        Channel& ch = m_channels[i];
        if (!m_enabled[static_cast<int>(ch.param)] || !InExplicitScope(ch.monitorIndex, m_scope))
        {
            continue; // scroll gesture does not affect this channel right now
        }
        const int target = Clamp(ch.current + delta);
        if (target == ch.current)
        {
            continue; // already at a rail - nothing left to write
        }

        // Update the cached value immediately so rapid notches accumulate,
        // and REPLACE any queued target: intermediate values never reach the
        // hardware (last value wins).
        ch.current = target;
        m_pending[i] = target;
        anyPending = true;
    }

    if (anyPending)
    {
        m_lastInputTime = std::chrono::steady_clock::now();
        m_cv.notify_one();
    }
}

void AdjustmentCoordinator::AdjustTo(Param param, int value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started || m_stopping || !m_ready)
    {
        return;
    }

    const int target = Clamp(value);
    bool anyPending = false;
    for (size_t i = 0; i < m_channels.size(); ++i)
    {
        Channel& ch = m_channels[i];
        // Slider path: respects the monitor SCOPE but deliberately ignores
        // the scroll enable flags (UI spec section 3).
        if (ch.param != param || !InExplicitScope(ch.monitorIndex, m_scope))
        {
            continue;
        }
        if (target == ch.current)
        {
            continue;
        }
        ch.current = target;
        m_pending[i] = target;
        anyPending = true;
    }

    if (anyPending)
    {
        m_lastInputTime = std::chrono::steady_clock::now();
        m_cv.notify_one();
    }
}

void AdjustmentCoordinator::SetScope(int monitorIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scope = monitorIndex;
}

int AdjustmentCoordinator::Scope() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_scope;
}

void AdjustmentCoordinator::SetParamEnabled(Param param, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled[static_cast<int>(param)] = enabled;
}

bool AdjustmentCoordinator::IsParamEnabled(Param param) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_enabled[static_cast<int>(param)];
}

void AdjustmentCoordinator::SetScrollStep(int step)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scrollStep = step < 1 ? 1 : step;
}

int AdjustmentCoordinator::ScrollStep() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_scrollStep;
}

void AdjustmentCoordinator::RequestReenumerate()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started || m_stopping)
        {
            return;
        }
        // Coalesce bursts (display change + device change often arrive
        // together, plus a power-resume): one flag, last request wins.
        m_reenumerateRequested = true;
    }
    m_cv.notify_all();
}

bool AdjustmentCoordinator::Ready() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ready;
}

bool AdjustmentCoordinator::SupportsParam(Param param, int monitorIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_ready)
    {
        return false;
    }
    for (const Channel& ch : m_channels)
    {
        if (ch.param == param && InExplicitScope(ch.monitorIndex, monitorIndex))
        {
            return true;
        }
    }
    return false;
}

int AdjustmentCoordinator::GetValue(Param param, int monitorIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_ready)
    {
        return kValueUnsupported;
    }
    int first = kValueUnsupported;
    bool found = false;
    for (const Channel& ch : m_channels)
    {
        if (ch.param != param || !InExplicitScope(ch.monitorIndex, monitorIndex))
        {
            continue;
        }
        if (!found)
        {
            first = ch.current;
            found = true;
        }
        else if (ch.current != first)
        {
            return kValueMixed;
        }
    }
    return found ? first : kValueUnsupported;
}

std::vector<AdjustmentCoordinator::Channel> AdjustmentCoordinator::BuildFreshChannels(MonitorManager* monitors)
{
    std::vector<Channel> channels;

    // M12 policy change - TRUST THE BUS, NOT THE BROCHURE: channel support
    // is decided by a successful ReadVcp reply alone. The capabilities
    // string is no longer consulted for gating. Evidence (docs/07 + M12
    // full-sweep probe): the reference panel advertises NOTHING in its caps
    // string (garbage reply) yet 59 codes answer on the bus - brightness,
    // contrast and all three R/G/B gains included. Caps-gated detection
    // would have blinded the whole app on such monitors; bus-trusted
    // detection also makes the app work identically on any monitor without
    // per-vendor tables. Cost is one extra read per candidate code at
    // startup/re-enumeration, already on the worker thread.
    static const struct { BYTE code; Param param; } singleCodeChannels[] = {
        { Vcp::Brightness, Param::Brightness },
        { Vcp::Contrast, Param::Contrast },
        { Vcp::RedGain, Param::RedGain },
        { Vcp::GreenGain, Param::GreenGain },
        { Vcp::BlueGain, Param::BlueGain },
        { Vcp::Volume, Param::Volume }, // M14: monitor speaker volume
    };

    int monitorIndex = 0;
    for (const auto& logical : monitors->Monitors())
    {
        for (const auto& physical : logical.physical)
        {
            const HANDLE raw = physical.handle.Get();
            const int idx = monitorIndex;

            // Single-code channels: brightness, contrast, R/G/B gains,
            // volume.
            for (const auto& single : singleCodeChannels)
            {
                const auto reading = m_bus->ReadVcp(raw, single.code);
                if (!reading)
                {
                    // No reply means no channel - that IS the capability
                    // check now. Without a baseline we could not apply
                    // relative steps anyway.
                    continue;
                }
                Channel ch;
                ch.monitor = raw;
                ch.codes = { single.code };
                // A zero maximum is a broken reply; VcpScaling treats it as
                // 100, mirror that here so the cached value agrees.
                ch.maxValue = reading->maximum == 0 ? 100u : reading->maximum;
                ch.current = Clamp(VcpScaling::ToPercent(reading->current, ch.maxValue));
                ch.param = single.param;
                ch.monitorIndex = idx;
                channels.push_back(std::move(ch));
            }

            // Saturation has no single MCCS code: it fans out over up to six
            // axis codes. They form ONE channel so one debounced write batch
            // keeps all axes in sync. An axis "exists" when it answers a
            // read - same bus-trust rule as above.
            std::vector<BYTE> axes;
            uint32_t axisMax = 100;
            int32_t axisCurrent = 0;
            for (size_t i = 0; i < Vcp::SaturationAxisCount; ++i)
            {
                if (const auto reading = m_bus->ReadVcp(raw, Vcp::SaturationAxes[i]))
                {
                    axes.push_back(Vcp::SaturationAxes[i]);
                    // Baseline comes from the FIRST responding axis; the
                    // fan-out write keeps them equal afterwards.
                    if (axes.size() == 1)
                    {
                        axisMax = reading->maximum == 0 ? 100u : reading->maximum;
                        axisCurrent = Clamp(VcpScaling::ToPercent(reading->current, axisMax));
                    }
                }
            }
            if (!axes.empty())
            {
                Channel ch;
                ch.monitor = raw;
                ch.codes = std::move(axes);
                ch.maxValue = axisMax;
                ch.current = axisCurrent;
                ch.param = Param::Saturation;
                ch.monitorIndex = idx;
                channels.push_back(std::move(ch));
            }
        }
        ++monitorIndex;
    }

    return channels;
}

void AdjustmentCoordinator::WorkerLoop()
{
    // Everything below used to run on the CALLER's thread, inside Start().
    // That caller is the UI thread, which by the time Start() runs already
    // owns (or is about to own) a process-wide WH_MOUSE_LL hook. Blocking
    // it here on DDC/CI I/O - a capability-string round trip per monitor,
    // then one VCP read per channel, each easily 50-300ms - froze mouse
    // input for the ENTIRE SYSTEM, not just this app, for as long as
    // enumeration took. Doing it here, on a thread with no hook and no
    // message queue anyone depends on, costs nothing but a slightly later
    // "ready" state.
    //
    // M6: the loop now also services RequestReenumerate() (hot-plug,
    // sleep/wake). Start() primes the flag so the FIRST pass through the
    // body performs the initial enumeration - there is exactly one code
    // path for both. (LoadChannelsForTest leaves the flag false and never
    // sets m_monitors: the test seam runs this same loop against an
    // injected IVcpBus with no enumeration pass at all.)
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;)
    {
        if (!m_reenumerateRequested)
        {
            if (m_pending.empty())
            {
                m_cv.wait(lock, [this] { return m_stopping || m_reenumerateRequested || !m_pending.empty(); });
            }
            else
            {
                // Fresh input extends the quiet window: wait until at least
                // kDebounceWindowMs have passed since the LAST input, no
                // matter how many arrived meanwhile.
                const auto deadline = m_lastInputTime + std::chrono::milliseconds(kDebounceWindowMs);
                if (m_cv.wait_until(lock, deadline) == std::cv_status::no_timeout)
                {
                    continue; // new input, stop request or re-enum - re-evaluate
                }
            }
        }

        if (m_stopping)
        {
            return;
        }

        if (m_reenumerateRequested)
        {
            m_reenumerateRequested = false;
            lock.unlock();
            m_monitors->Refresh(); // RAII-disposes stale handles, enumerates anew
            std::vector<Channel> fresh = BuildFreshChannels(m_monitors);
            bool anyChannels = false;
            {
                lock.lock();
                // Atomic swap: query APIs on the UI thread always observe a
                // complete old OR complete new set. Pending targets die with
                // the old set - their indices may not exist in the new one.
                m_channels.swap(fresh);
                m_pending.clear();
                anyChannels = !m_channels.empty();
                m_ready = true;
                if (anyChannels)
                {
                    m_lastInputTime = std::chrono::steady_clock::now();
                }
            }
            if (m_onReady)
            {
                // Outside the lock; App marshals to the UI thread itself.
                m_onReady(anyChannels);
            }
            continue;
        }

        // Quiet for a full window: swap the batch out and write WITHOUT
        // holding the lock, so input keeps flowing during the ~50 ms per
        // SetVCPFeature calls.
        std::unordered_map<size_t, int> batch;
        batch.swap(m_pending);
        lock.unlock();

        for (const auto& entry : batch)
        {
            const Channel& ch = m_channels[entry.first];
            // Percent target -> raw device units through THIS channel's
            // reported range (identity on max=100 panels; correct on the
            // Samsung-style max=50 ones, docs/07).
            const uint32_t target = VcpScaling::ToRaw(Clamp(entry.second), ch.maxValue);
            for (const BYTE code : ch.codes)
            {
                // Per-code failures are isolated on purpose: one stuck or
                // unsupported axis must never abort the rest of the batch.
                // NO synchronous verification afterwards - reads right after
                // a write can return stale values (M1 checkpoint fact).
                DWORD error = 0;
                if (!m_bus->WriteVcp(ch.monitor, code, target, &error) &&
                    DdcErrors::Classify(error) == DdcErrors::Class::Transient)
                {
                    // I2C blip: exactly one immediate retry, then give up
                    // like any terminal failure. Unknown errors get NO retry
                    // (DdcErrors policy) - this machine's wedge history.
                    Sleep(kWriteRetryDelayMs);
                    m_bus->WriteVcp(ch.monitor, code, target, nullptr);
                }
            }
        }

        lock.lock();
    }
}

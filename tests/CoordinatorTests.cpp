#include "TestHarness.h"
#include "AdjustmentCoordinator.h"
#include "VcpCodes.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace
{
    /// Opaque monitor tokens so fake-bus expectations can tell channels
    /// apart without any real handle.
    HANDLE Token(int id)
    {
        return reinterpret_cast<HANDLE>(static_cast<intptr_t>(id));
    }

    /// IVcpBus fake: records every write (thread-safely - the coordinator's
    /// worker thread appends while the test main thread polls) and answers
    /// nothing on reads.
    class FakeBus final : public IVcpBus
    {
    public:
        struct Write
        {
            HANDLE monitor;
            BYTE code;
            uint32_t value;
        };

        std::optional<NativeMonitorApi::VcpReading> ReadVcp(HANDLE, BYTE) override
        {
            return std::nullopt;
        }

        bool WriteVcp(HANDLE monitor, BYTE code, uint32_t value, DWORD* errorOut) override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            writes.push_back(Write{monitor, code, value});
            if (errorOut)
            {
                *errorOut = 0;
            }
            return true;
        }

        size_t Count()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return writes.size();
        }

        std::vector<Write> Snapshot()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return writes;
        }

    private:
        std::mutex m_mutex;
        std::vector<Write> writes;
    };

    /// Polls until |pred| holds or |timeoutMs| elapse (the flush happens on
    /// the coordinator's worker after the 100 ms debounce window).
    bool WaitFor(const std::function<bool()>& pred, int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
            {
                return true;
            }
            Sleep(10);
        }
        return pred();
    }

    /// Quiet period longer than one debounce window, so a misbehaving
    /// coalescer would have flushed any extra writes by now.
    void SettlePastDebounce()
    {
        Sleep(AdjustmentCoordinator::kDebounceWindowMs + 150);
    }

    void ScrollBurstCoalescesToOneWriteAtFinalValue()
    {
        AdjustmentCoordinator coordinator;
        FakeBus bus;
        coordinator.LoadChannelsForTest(
            {{Param::Brightness, Token(1), 0, {Vcp::Brightness}, 50, 100}}, &bus);

        for (int i = 0; i < 5; ++i)
        {
            coordinator.AdjustAll(1); // default step 2 -> target 60
        }
        // Cache updates synchronously on the calling thread.
        CHECK(coordinator.GetValue(Param::Brightness, -1) == 60);

        CHECK(WaitFor([&] { return bus.Count() >= 1; }, 2000));
        SettlePastDebounce();
        CHECK(bus.Count() == 1); // last value wins: ONE write, not five
        const auto writes = bus.Snapshot();
        CHECK(writes[0].monitor == Token(1));
        CHECK(writes[0].code == Vcp::Brightness);
        CHECK(writes[0].value == 60);
    }

    void DisabledParamSkipsScrollButNotSlider()
    {
        AdjustmentCoordinator coordinator;
        FakeBus bus;
        // Volume defaults to scroll-DISABLED (M14).
        coordinator.LoadChannelsForTest(
            {{Param::Volume, Token(1), 0, {Vcp::Volume}, 30, 100}}, &bus);

        coordinator.AdjustAll(5);
        SettlePastDebounce();
        CHECK(bus.Count() == 0);

        // Slider path deliberately bypasses the enable flags (UI spec 3).
        coordinator.AdjustTo(Param::Volume, 77);
        CHECK(coordinator.GetValue(Param::Volume, -1) == 77);
        CHECK(WaitFor([&] { return bus.Count() == 1; }, 2000));
        CHECK(bus.Snapshot()[0].value == 77);
    }

    void ScopeRestrictsWritesToSelectedMonitor()
    {
        AdjustmentCoordinator coordinator;
        FakeBus bus;
        coordinator.LoadChannelsForTest(
            {
                {Param::Brightness, Token(1), 0, {Vcp::Brightness}, 40, 100},
                {Param::Brightness, Token(2), 1, {Vcp::Brightness}, 60, 100},
            },
            &bus);

        coordinator.SetScope(1);
        coordinator.AdjustTo(Param::Brightness, 55);
        CHECK(WaitFor([&] { return bus.Count() >= 1; }, 2000));
        SettlePastDebounce();
        CHECK(bus.Count() == 1);
        const auto writes = bus.Snapshot();
        CHECK(writes[0].monitor == Token(2)); // monitor 0 untouched
        CHECK(writes[0].value == 55);
    }

    void RailClampProducesNoWrite()
    {
        // One coordinator per rail: AdjustAll moves every enabled param, so
        // a top-rail brightness would legitimately scroll DOWN with the
        // same call that tests the bottom rail.
        FakeBus topBus;
        {
            AdjustmentCoordinator top;
            top.LoadChannelsForTest(
                {{Param::Brightness, Token(1), 0, {Vcp::Brightness}, 100, 100}}, &topBus);
            top.AdjustAll(4); // already at the top rail
            CHECK(top.GetValue(Param::Brightness, -1) == 100);
        }
        FakeBus bottomBus;
        {
            AdjustmentCoordinator bottom;
            bottom.LoadChannelsForTest(
                {{Param::Contrast, Token(1), 0, {Vcp::Contrast}, 0, 100}}, &bottomBus);
            bottom.AdjustAll(-4); // already at the bottom rail
            CHECK(bottom.GetValue(Param::Contrast, -1) == 0);
        }
        // Both workers stopped (destructors joined them): nothing pending
        // ever reached the bus.
        SettlePastDebounce();
        CHECK(topBus.Count() == 0);
        CHECK(bottomBus.Count() == 0);
    }

    void PercentTargetsScaleThroughDeviceMax()
    {
        AdjustmentCoordinator coordinator;
        FakeBus bus;
        // Samsung-style max=50 panel (docs/07): 50 percent must arrive as
        // raw 25, not 50.
        coordinator.LoadChannelsForTest(
            {{Param::Brightness, Token(1), 0, {Vcp::Brightness}, 20, 50}}, &bus);

        coordinator.AdjustTo(Param::Brightness, 50);
        CHECK(WaitFor([&] { return bus.Count() == 1; }, 2000));
        CHECK(bus.Snapshot()[0].value == 25);
    }

    void SaturationFansOutAcrossAxes()
    {
        AdjustmentCoordinator coordinator;
        FakeBus bus;
        coordinator.LoadChannelsForTest(
            {{Param::Saturation, Token(1), 0, {0x59, 0x5B}, 50, 100}}, &bus);

        coordinator.AdjustTo(Param::Saturation, 80);
        CHECK(WaitFor([&] { return bus.Count() == 2; }, 2000));
        SettlePastDebounce();
        CHECK(bus.Count() == 2); // one batch keeps all axes in sync
        const auto writes = bus.Snapshot();
        CHECK(writes[0].value == 80 && writes[1].value == 80);
        CHECK((writes[0].code == 0x59 && writes[1].code == 0x5B) ||
              (writes[0].code == 0x5B && writes[1].code == 0x59));
    }

    void QuerySemanticsMixedUnsupportedScoped()
    {
        AdjustmentCoordinator coordinator;
        FakeBus bus;
        coordinator.LoadChannelsForTest(
            {
                {Param::Brightness, Token(1), 0, {Vcp::Brightness}, 30, 100},
                {Param::Brightness, Token(2), 1, {Vcp::Brightness}, 90, 100},
            },
            &bus);

        CHECK(coordinator.SupportsParam(Param::Brightness, -1));
        CHECK(coordinator.GetValue(Param::Brightness, -1) == AdjustmentCoordinator::kValueMixed);
        CHECK(!coordinator.SupportsParam(Param::Volume, -1)); // no volume channels exist
        CHECK(coordinator.GetValue(Param::Volume, -1) == AdjustmentCoordinator::kValueUnsupported);
        CHECK(coordinator.GetValue(Param::Brightness, 0) == 30); // scoped query agrees
        CHECK(coordinator.Ready());
    }

    void ScrollStepScalesEachNotch()
    {
        AdjustmentCoordinator coordinator;
        FakeBus bus;
        coordinator.LoadChannelsForTest(
            {{Param::Brightness, Token(1), 0, {Vcp::Brightness}, 50, 100}}, &bus);
        coordinator.SetScrollStep(10);

        coordinator.AdjustAll(2); // 2 notches x step 10 -> 70
        CHECK(coordinator.GetValue(Param::Brightness, -1) == 70);
        CHECK(WaitFor([&] { return bus.Count() == 1; }, 2000));
        CHECK(bus.Snapshot()[0].value == 70);
    }
}

void RunCoordinatorTests()
{
    ScrollBurstCoalescesToOneWriteAtFinalValue();
    DisabledParamSkipsScrollButNotSlider();
    ScopeRestrictsWritesToSelectedMonitor();
    RailClampProducesNoWrite();
    PercentTargetsScaleThroughDeviceMax();
    SaturationFansOutAcrossAxes();
    QuerySemanticsMixedUnsupportedScoped();
    ScrollStepScalesEachNotch();
}

#include "TestHarness.h"
#include "VcpScaling.h"
#include "DdcErrors.h"

#include <cstdint>

namespace
{
    void MaxOf100IsAnIdentity()
    {
        for (int p = 0; p <= 100; ++p)
        {
            CHECK(VcpScaling::ToRaw(p, 100) == static_cast<uint32_t>(p));
        }
        for (uint32_t r = 0; r <= 100; ++r)
        {
            CHECK(VcpScaling::ToPercent(r, 100) == static_cast<int>(r));
        }
    }

    void HalfRangeMapsCorrectly()
    {
        // The Samsung-style max=50 case from docs/07.
        CHECK(VcpScaling::ToRaw(0, 50) == 0);
        CHECK(VcpScaling::ToRaw(1, 50) == 1);   // 0.5 rounds half-up
        CHECK(VcpScaling::ToRaw(25, 50) == 13); // 12.5 rounds half-up
        CHECK(VcpScaling::ToRaw(50, 50) == 25);
        CHECK(VcpScaling::ToRaw(75, 50) == 38); // 37.5 rounds half-up
        CHECK(VcpScaling::ToRaw(99, 50) == 50); // 49.5 rounds to the rail
        CHECK(VcpScaling::ToRaw(100, 50) == 50);

        CHECK(VcpScaling::ToPercent(0, 50) == 0);
        CHECK(VcpScaling::ToPercent(13, 50) == 26);
        CHECK(VcpScaling::ToPercent(25, 50) == 50);
        CHECK(VcpScaling::ToPercent(38, 50) == 76);
        CHECK(VcpScaling::ToPercent(49, 50) == 98);
        CHECK(VcpScaling::ToPercent(50, 50) == 100);
    }

    void OddRangesRoundHalfUp()
    {
        // max=3: raw = round(p*3/100). 17% -> 0.51 -> 1; 83% -> 2.49 -> 2.
        CHECK(VcpScaling::ToRaw(17, 3) == 1);
        CHECK(VcpScaling::ToRaw(83, 3) == 2);
        CHECK(VcpScaling::ToPercent(1, 3) == 33);  // 33.33 -> 33
        CHECK(VcpScaling::ToPercent(2, 3) == 67);  // 66.67 -> 67
    }

    void BrokenAndOutOfRangeInputsStaySane()
    {
        // max=0 guard behaves exactly like max=100.
        CHECK(VcpScaling::ToRaw(50, 0) == 50);
        CHECK(VcpScaling::ToPercent(50, 0) == 50);
        // Out-of-range inputs clamp at the rails.
        CHECK(VcpScaling::ToRaw(-10, 50) == 0);
        CHECK(VcpScaling::ToRaw(150, 50) == 50);
        CHECK(VcpScaling::ToPercent(60, 50) == 100);
    }

    void ErrorClassifierSortsTheKnownCodes()
    {
        using C = DdcErrors::Class;
        CHECK(DdcErrors::Classify(0) == C::Success);
        // Terminal: capability facts and dead handles never retry.
        CHECK(DdcErrors::Classify(DdcErrors::kDdcciVcpNotSupported) == C::Terminal);
        CHECK(DdcErrors::Classify(DdcErrors::kUnsupportedVcpCodeLegacy) == C::Terminal);
        CHECK(DdcErrors::Classify(DdcErrors::kInvalidPhysicalMonitorHandle) == C::Terminal);
        CHECK(DdcErrors::Classify(DdcErrors::kMonitorNoLongerExists) == C::Terminal);
        // Transient: I2C framing/checksum/timeout family retries once.
        CHECK(DdcErrors::Classify(ERROR_TIMEOUT) == C::Transient);
        CHECK(DdcErrors::Classify(0xC0262582ul) == C::Transient);
        CHECK(DdcErrors::Classify(0xC026258Bul) == C::Transient);
        // Unknown codes get NO retry by design (wedge history on this machine).
        CHECK(DdcErrors::Classify(5) == C::Terminal);
        CHECK(DdcErrors::Classify(ERROR_ACCESS_DENIED) == C::Terminal);
    }
}

void RunVcpScalingTests()
{
    MaxOf100IsAnIdentity();
    HalfRangeMapsCorrectly();
    OddRangesRoundHalfUp();
    BrokenAndOutOfRangeInputsStaySane();
    ErrorClassifierSortsTheKnownCodes();
}

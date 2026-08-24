#include "TestHarness.h"
#include "VcpCodes.h"

#include <set>

namespace
{
    void BrightnessAndContrastConstantsAreCorrect()
    {
        CHECK(Vcp::Brightness == 0x10);
        CHECK(Vcp::Contrast == 0x12);
    }

    void SaturationAxesMatchTheSixDocumentedCodes()
    {
        const BYTE expected[6] = { 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E };
        CHECK(Vcp::SaturationAxisCount == 6);
        for (size_t i = 0; i < 6; ++i)
        {
            CHECK(Vcp::SaturationAxes[i] == expected[i]);
        }
    }

    void AllDefinedCodesAreDistinct()
    {
        std::set<BYTE> unique;
        unique.insert(Vcp::Brightness);
        unique.insert(Vcp::Contrast);
        for (const BYTE axis : Vcp::SaturationAxes)
        {
            unique.insert(axis);
        }
        CHECK(unique.size() == 2 + Vcp::SaturationAxisCount);
    }
}

void RunVcpCodesTests()
{
    BrightnessAndContrastConstantsAreCorrect();
    SaturationAxesMatchTheSixDocumentedCodes();
    AllDefinedCodesAreDistinct();
}

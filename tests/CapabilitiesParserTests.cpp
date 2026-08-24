#include "TestHarness.h"
#include "CapabilitiesParser.h"
#include "VcpCodes.h"

#include <iostream>
#include <string>
#include <set>

namespace
{
    // Captured verbatim from M0 checkpoint 1 (human's monitor, LG-class
    // ultrawide). Note the quirks it exercises: model name glued to cmds
    // ("UL550cmds"), value lists with leading/trailing inner spaces
    // ("60( 11 12 0F 00)", "14(05 08 0B )").
    const std::string kCapturedUltraWide =
        "(prot(monitor)type(lcd)UL550cmds(01 02 03 0C E3 F3)vcp(02 04 05 08 10 12 14(05 08 0B ) 16 18 1A 52 60( 11 12 0F 00) AC AE B2 B6 C0 C6 C8 C9 D6(01 04) DF 62 8D F4 F5(00 01 02) F6(00 01 02) 4D 4E 4F 15(01 06 11 13 14 18 28 29 32 48) F7(00 01 02 03) F8(00 01) F9 E4 E5 E6 E7 E8 E9 EA EB EF FD(00 01) FE(00 01 02) FF)mccs_ver(2.1)mswhql(1))";

    bool Contains(const std::set<BYTE>& codes, BYTE code)
    {
        return codes.find(code) != codes.end();
    }

    void ParsesTheCapturedM0CapabilityString()
    {
        const std::set<BYTE> codes = CapabilitiesParser::ParseSupportedVcpCodes(kCapturedUltraWide);

        // The two codes LiteDDC depends on most.
        CHECK(Contains(codes, Vcp::Brightness));
        CHECK(Contains(codes, Vcp::Contrast));

        // Saturation axes: claimed by NO monitor under test -> all absent.
        for (const BYTE axis : Vcp::SaturationAxes)
        {
            CHECK(!Contains(codes, axis));
        }

        // Spot-check unrelated codes present/absent in the captured string.
        CHECK(Contains(codes, 0x62));
        CHECK(Contains(codes, 0xD6));
        CHECK(!Contains(codes, 0x6C));
    }

    void HandlesValueListsAndCaseInsensitivity()
    {
        const std::string mixed = "(type(lcd)VCP(10(01 02) 12 59 5a 5E))";
        const std::set<BYTE> codes = CapabilitiesParser::ParseSupportedVcpCodes(mixed);
        CHECK(codes.size() == 5);
        CHECK(Contains(codes, 0x10));
        CHECK(Contains(codes, 0x12));
        CHECK(Contains(codes, 0x59));
        CHECK(Contains(codes, 0x5A));
        CHECK(Contains(codes, 0x5E));
    }

    void CollapsesDuplicates()
    {
        const std::string dup = "(vcp(10 10 12 10))";
        const std::set<BYTE> codes = CapabilitiesParser::ParseSupportedVcpCodes(dup);
        CHECK(codes.size() == 2);
    }

    void ReturnsEmptyForMissingOrEmptyVcpSegment()
    {
        CHECK(CapabilitiesParser::ParseSupportedVcpCodes("").empty());
        CHECK(CapabilitiesParser::ParseSupportedVcpCodes("(prot(monitor)mccs_ver(2.1))").empty());
        CHECK(CapabilitiesParser::ParseSupportedVcpCodes("(vcp())").empty());
        CHECK(CapabilitiesParser::ParseSupportedVcpCodes("(vcpp(10)cmds(01))").empty());
    }

    void ToleratesMalformedInput()
    {
        // Unbalanced/truncated strings must not loop forever or throw.
        CHECK(CapabilitiesParser::ParseSupportedVcpCodes("(vcp(10 12").size() <= 2);
        CHECK(CapabilitiesParser::ParseSupportedVcpCodes("(vcp(10 1x2 zz))").size() <= 1);
        CHECK(CapabilitiesParser::ParseSupportedVcpCodes("))))vcp(").empty());
    }
}

void RunCapabilitiesParserTests()
{
    ParsesTheCapturedM0CapabilityString();
    HandlesValueListsAndCaseInsensitivity();
    CollapsesDuplicates();
    ReturnsEmptyForMissingOrEmptyVcpSegment();
    ToleratesMalformedInput();
}

void RunVcpCodesTests();
void RunSettingsTests();
void RunVcpScalingTests();
void RunCoordinatorTests();
void RunMonitorManagerTests();

int main()
{
    RunCapabilitiesParserTests();
    RunVcpCodesTests();
    RunSettingsTests();
    RunVcpScalingTests();
    RunCoordinatorTests();
    RunMonitorManagerTests();

    const int failures = TestHarness::FailureCount();
    if (failures == 0)
    {
        std::cout << "DdcTests: ALL PASSED\n";
        return 0;
    }
    std::cout << "DdcTests: " << failures << " FAILURE(S)\n";
    return 1;
}

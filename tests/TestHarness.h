#pragma once

// Shared test fixtures and macros for DdcTests (no external test framework).
//
// Reused from LiteZones' TestHarness.h: the reporting/check machinery below
// is verbatim. The LiteZones-specific zone/layout fixture helpers were
// removed because layout/, overlay/, editor/ are deliberately not reused
// (see docs/ROADMAP.md "Non-negotiables"); new LiteDDC-specific helpers get
// added here as milestones need them.

#include <windows.h>

#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace TestHarness
{
    /// Total failed checks across all translation units so far. C++17
    /// inline variable: ONE program-wide instance (an anonymous-namespace
    /// counter here would give every .cpp its own tally that main() never
    /// sees - silently ignoring other files' failures).
    inline int g_failures = 0;

    /// Total failed checks across all translation units so far.
    inline int FailureCount()
    {
        return g_failures;
    }
}

namespace
{
    void Report(bool ok, const char* file, int line, const std::string& expr)
    {
        if (!ok)
        {
            ++TestHarness::g_failures;
            std::cerr << "FAIL " << file << ":" << line << ": " << expr << "\n";
        }
    }

#define CHECK(expr) Report((expr), __FILE__, __LINE__, #expr)

    void checkRectsEqual(const RECT& expected, const RECT& actual, const char* file, int line)
    {
        Report(expected.left == actual.left && expected.right == actual.right &&
                   expected.top == actual.top && expected.bottom == actual.bottom,
               file, line, "rectangles are equal");
    }

#define CHECK_RECT(a, b) checkRectsEqual((a), (b), __FILE__, __LINE__)
}

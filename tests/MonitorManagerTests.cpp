#include "TestHarness.h"
#include "MonitorManager.h"

namespace
{
    void InternalTechnologyClassification()
    {
        // Technologies that mark chassis-internal panels (filtered out of
        // enumeration); everything else stays listed.
        CHECK(MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL));
        CHECK(MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS));
        CHECK(MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED));
        CHECK(MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED));

        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER));
        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15));
        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI));
        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI));
        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL));
        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EXTERNAL));
        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST));
        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED));
        CHECK(!MonitorManager::IsInternalTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL));
    }

    void GenericDisplayNameGuard()
    {
        CHECK(MonitorManager::IsGenericDisplayName(nullptr));
        CHECK(MonitorManager::IsGenericDisplayName(L""));
        CHECK(MonitorManager::IsGenericDisplayName(L"Generic PnP Monitor"));
        CHECK(MonitorManager::IsGenericDisplayName(L"Generic Non-PnP Monitor"));
        CHECK(MonitorManager::IsGenericDisplayName(L"PnP-Monitor (Standard)"));
        CHECK(!MonitorManager::IsGenericDisplayName(L"DELL U2723QE"));
        CHECK(!MonitorManager::IsGenericDisplayName(L"LG HDR 4K"));
    }
}

void RunMonitorManagerTests()
{
    InternalTechnologyClassification();
    GenericDisplayNameGuard();
}

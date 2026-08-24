#include "NativeMonitorApi.h"
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#include <cstring>

// Not exported by the dxva2 headers; documented as the failure code
// GetVCPFeatureAndVCPFeatureReply/SetVCPFeature produce for codes the
// monitor does not implement. Value matches the well-known dxva2 error
// facility used by other DDC tools.
#ifndef ERROR_UNSUPPORTED_VCP_CODE
#define ERROR_UNSUPPORTED_VCP_CODE 0xC1FF0000L
#endif

namespace
{
    bool IsUnsupportedVcpCodeError(DWORD error)
    {
        return error == ERROR_UNSUPPORTED_VCP_CODE;
    }
}

std::optional<std::string> NativeMonitorApi::GetCapabilitiesString(HANDLE physicalMonitor)
{
    DWORD length = 0;
    if (!GetCapabilitiesStringLength(physicalMonitor, &length) || length == 0)
    {
        return std::nullopt;
    }

    // The API writes a null-terminated string; give it one spare char.
    std::string capabilities(length + 1, '\0');
    if (!CapabilitiesRequestAndCapabilitiesReply(physicalMonitor, &capabilities[0], length + 1))
    {
        return std::nullopt;
    }
    capabilities.resize(std::strlen(capabilities.c_str()));
    return capabilities;
}

std::optional<NativeMonitorApi::VcpReading> NativeMonitorApi::ReadVcp(HANDLE physicalMonitor, BYTE code)
{
    DWORD currentValue = 0;
    DWORD maximumValue = 0;
    MC_VCP_CODE_TYPE valueType = MC_MOMENTARY;
    if (!GetVCPFeatureAndVCPFeatureReply(physicalMonitor, code, &valueType, &currentValue, &maximumValue))
    {
        // ERROR_UNSUPPORTED_VCP_CODE lands here like any other failure:
        // "control not available", not an error to escalate.
        return std::nullopt;
    }
    VcpReading reading;
    reading.current = static_cast<uint32_t>(currentValue);
    reading.maximum = static_cast<uint32_t>(maximumValue);
    return reading;
}

bool NativeMonitorApi::WriteVcp(HANDLE physicalMonitor, BYTE code, uint32_t value, DWORD* errorOut)
{
    if (errorOut)
    {
        *errorOut = 0;
    }
    // Blocks ~50 ms per Microsoft's documentation - see header note.
    if (SetVCPFeature(physicalMonitor, code, value) != FALSE)
    {
        return true;
    }
    if (errorOut)
    {
        *errorOut = GetLastError();
    }
    return false;
}

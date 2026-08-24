#include "VcpBus.h"

std::optional<NativeMonitorApi::VcpReading> NativeVcpBus::ReadVcp(HANDLE physicalMonitor, BYTE code)
{
    return NativeMonitorApi::ReadVcp(physicalMonitor, code);
}

bool NativeVcpBus::WriteVcp(HANDLE physicalMonitor, BYTE code, uint32_t value, DWORD* errorOut)
{
    return NativeMonitorApi::WriteVcp(physicalMonitor, code, value, errorOut);
}

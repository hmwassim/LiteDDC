#pragma once

#include "NativeMonitorApi.h"

#include <optional>

/// Injectable seam for VCP transport (pre-release review item 8): the
/// narrow "physical monitor access" boundary AdjustmentCoordinator actually
/// touches. Production code always runs against NativeVcpBus; tests inject
/// a fake implementation so the coalescing/scope/scaling logic can be
/// regression-tested without any monitor hardware attached.
///
/// Semantics mirror NativeMonitorApi exactly:
/// - ReadVcp returns nullopt on ANY failure including unsupported codes -
///   "no reply" is a normal capability outcome here.
/// - WriteVcp returns false on failure and reports GetLastError() through
///   |errorOut| when non-null (0 on success).
class IVcpBus
{
public:
    virtual ~IVcpBus() = default;

    virtual std::optional<NativeMonitorApi::VcpReading> ReadVcp(HANDLE physicalMonitor, BYTE code) = 0;
    virtual bool WriteVcp(HANDLE physicalMonitor, BYTE code, uint32_t value, DWORD* errorOut) = 0;
};

/// Production implementation: straight through to NativeMonitorApi/dxva2.
class NativeVcpBus final : public IVcpBus
{
public:
    std::optional<NativeMonitorApi::VcpReading> ReadVcp(HANDLE physicalMonitor, BYTE code) override;
    bool WriteVcp(HANDLE physicalMonitor, BYTE code, uint32_t value, DWORD* errorOut) override;
};

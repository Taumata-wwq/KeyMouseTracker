#pragma once

#include "render_window.h"

#include <cstdint>

namespace ui {

enum class RenderDeviceStatus : uint8_t {
    Ok,
    RecreateTarget,
    DeviceRemoved,
    DeviceReset,
    DeviceHung,
    UnknownFailure,
};

class RenderResources {
public:
    void ClearAll();
    void ClearWindow(RenderWindowId id);
    void MarkDeviceLost(RenderDeviceStatus status = RenderDeviceStatus::UnknownFailure);
    void MarkDeviceRecovered();

    uint64_t DeviceGeneration() const { return deviceGeneration_; }
    bool DeviceLost() const { return deviceLost_; }
    RenderDeviceStatus LastDeviceStatus() const { return lastDeviceStatus_; }

private:
    uint64_t deviceGeneration_ = 1;
    bool deviceLost_ = false;
    RenderDeviceStatus lastDeviceStatus_ = RenderDeviceStatus::Ok;
};

} // namespace ui

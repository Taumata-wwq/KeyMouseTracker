#include "render_resources.h"

namespace ui {

void RenderResources::ClearAll() {
    ++deviceGeneration_;
    deviceLost_ = false;
    lastDeviceStatus_ = RenderDeviceStatus::Ok;
}

void RenderResources::ClearWindow(RenderWindowId) {
    // GPU resources are introduced later; the skeleton keeps the lifecycle hook.
}

void RenderResources::MarkDeviceLost(RenderDeviceStatus status) {
    ++deviceGeneration_;
    deviceLost_ = true;
    lastDeviceStatus_ = status;
}

void RenderResources::MarkDeviceRecovered() {
    deviceLost_ = false;
    lastDeviceStatus_ = RenderDeviceStatus::Ok;
}

} // namespace ui

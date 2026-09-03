#pragma once

#include <d2d1.h>
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

namespace ui {

struct FrameJob;
class Renderer;

struct RenderWindowId {
    uint64_t window_id = 0;
    uint64_t hwnd_generation = 0;

    bool IsValid() const { return window_id != 0 && hwnd_generation != 0; }
    bool operator==(const RenderWindowId& other) const noexcept {
        return window_id == other.window_id &&
               hwnd_generation == other.hwnd_generation;
    }
};

enum class RenderSurfaceKind : uint8_t {
    HwndSwapChain,
    LayeredComposition,
};

enum class RenderFrameResult : uint8_t {
    Skipped,
    Prepared,
    Presented,
    InvalidWindow,
    RecreateTarget,
    DeviceFailure,
};

class RenderWindow {
public:
    RenderWindow() = default;
    RenderWindow(RenderWindowId id, HWND hwnd,
                 RenderSurfaceKind surfaceKind = RenderSurfaceKind::HwndSwapChain);
    ~RenderWindow();

    RenderWindow(const RenderWindow&) = delete;
    RenderWindow& operator=(const RenderWindow&) = delete;
    RenderWindow(RenderWindow&&) noexcept;
    RenderWindow& operator=(RenderWindow&&) noexcept;

    RenderWindowId Id() const { return id_; }
    HWND Hwnd() const { return hwnd_; }
    RenderSurfaceKind SurfaceKind() const { return surfaceKind_; }
    bool IsRegistered() const { return id_.IsValid() && hwnd_ != nullptr; }
    bool Matches(RenderWindowId id) const { return id_ == id; }

    void Register(RenderWindowId id, HWND hwnd,
                  RenderSurfaceKind surfaceKind = RenderSurfaceKind::HwndSwapChain);
    void Unregister();
    void ReleaseDeviceResources();
    RenderFrameResult RenderFrame(const FrameJob& job);
    RenderFrameResult PresentPrepared(bool skipVSync);
    int ScreenshotRegion(D2D1_RECT_F region, const std::wstring& outPath, float dpiScale);

private:
    void ReleaseResources();

    RenderWindowId id_{};
    HWND hwnd_ = nullptr;
    RenderSurfaceKind surfaceKind_ = RenderSurfaceKind::HwndSwapChain;
    std::unique_ptr<Renderer> renderer_;
    bool rendererReady_ = false;
    bool targetReady_ = false;
    int widthPx_ = 0;
    int heightPx_ = 0;
};

} // namespace ui

#pragma once

#include "display_list.h"
#include "frame_scheduler.h"
#include "render_resources.h"
#include "render_window.h"

#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>

namespace ui {

constexpr UINT kUiCoreRenderDeviceLostMessage = WM_APP + 131;

struct FrameJob {
    RenderWindowId window;
    HWND hwnd = nullptr;
    uint64_t generation = 0;
    int width_px = 0;
    int height_px = 0;
    float dpi_scale = 1.0f;
    PresentPolicy policy = PresentPolicy::Deferred;
    int priority = 0;
    bool render_thread_present = false;
    bool defer_present = false;
    DisplayList display_list;

    FrameJob() = default;
    FrameJob(const FrameJob&) = delete;
    FrameJob& operator=(const FrameJob&) = delete;
    FrameJob(FrameJob&&) noexcept = default;
    FrameJob& operator=(FrameJob&&) noexcept = default;
};

struct DeviceRecoveryRequest {
    RenderWindowId window;
    HWND hwnd = nullptr;
    RenderDeviceStatus status = RenderDeviceStatus::Ok;
    uint64_t failed_generation = 0;
    uint64_t device_generation = 0;
};

class RenderThread {
public:
    static RenderThread& Instance();

    ~RenderThread();

    void Start();
    void Stop();
    void RegisterWindow(RenderWindowId id, HWND hwnd,
                        RenderSurfaceKind surfaceKind = RenderSurfaceKind::HwndSwapChain);
    void UnregisterWindow(RenderWindowId id);
    void Submit(FrameJob job);
    bool PresentPrepared(RenderWindowId id, bool skipVSync, uint32_t timeoutMs);
    int ScreenshotRegion(RenderWindowId id, D2D1_RECT_F region, std::wstring outPath, float dpiScale);
    bool WaitForGeneration(uint64_t windowId, uint64_t generation, uint32_t timeoutMs);

    size_t RegisteredWindowCount() const;
    size_t PendingJobCount() const;
    uint64_t LastCompletedGeneration(uint64_t windowId) const;
    bool DeviceLostForTest() const;
    void ReportDeviceLostForTest(RenderWindowId failedWindow,
                                 RenderDeviceStatus status,
                                 uint64_t failedGeneration);
    std::vector<DeviceRecoveryRequest> ConsumeDeviceRecoveryRequestsForTest();

private:
    RenderThread() = default;
    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    void ThreadMain();
    std::optional<FrameJob> TakeNextJobLocked();
    void HandleDeviceLostLocked(RenderWindowId failedWindow,
                                RenderDeviceStatus status,
                                uint64_t failedGeneration,
                                std::vector<std::shared_ptr<RenderWindow>>& windowsToReset);

    struct ScreenshotJob {
        RenderWindowId window;
        D2D1_RECT_F region = {};
        std::wstring outPath;
        float dpiScale = 1.0f;
        std::promise<int> result;

        ScreenshotJob() = default;
        ScreenshotJob(const ScreenshotJob&) = delete;
        ScreenshotJob& operator=(const ScreenshotJob&) = delete;
        ScreenshotJob(ScreenshotJob&&) noexcept = default;
        ScreenshotJob& operator=(ScreenshotJob&&) noexcept = default;
    };

    struct PresentJob {
        RenderWindowId window;
        bool skipVSync = true;
        std::promise<bool> result;

        PresentJob() = default;
        PresentJob(const PresentJob&) = delete;
        PresentJob& operator=(const PresentJob&) = delete;
        PresentJob(PresentJob&&) noexcept = default;
        PresentJob& operator=(PresentJob&&) noexcept = default;
    };

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    bool running_ = false;
    bool stopRequested_ = false;
    std::unordered_map<uint64_t, std::shared_ptr<RenderWindow>> windows_;
    std::vector<std::shared_ptr<RenderWindow>> retiredWindows_;
    std::unordered_map<uint64_t, FrameJob> latestJobs_;
    std::deque<PresentJob> presentJobs_;
    std::deque<ScreenshotJob> screenshotJobs_;
    std::unordered_map<uint64_t, uint64_t> completedGenerations_;
    std::vector<DeviceRecoveryRequest> deviceRecoveryRequests_;
    RenderResources resources_;
};

} // namespace ui

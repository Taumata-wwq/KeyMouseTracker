#include "overlay_service.h"
#include "theme.h"

#include <d2d1.h>
#include <dwrite.h>
#include <timeapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <utility>

#pragma comment(lib, "winmm.lib")

namespace ui {
namespace {

constexpr UINT kOverlayWakeMessage = WM_APP + 0x540;
constexpr UINT_PTR kToastTimerId = 1;
constexpr UINT kToastTimerIntervalMs = 16;
constexpr int kToastInMs = 200;
constexpr int kToastOutMs = 250;

float EaseOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

UINT OverlayDpiForWindow(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using Fn = UINT(WINAPI*)(HWND);
        auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (fn) {
            UINT dpi = fn(hwnd);
            if (dpi) return dpi;
        }
    }
    HDC hdc = GetDC(hwnd);
    UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return dpi ? dpi : 96;
}

struct ToastBitmap {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    int width = 0;
    int height = 0;

    ~ToastBitmap() {
        if (dc && oldBitmap) {
            SelectObject(dc, oldBitmap);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
    }
};

struct ToastOverlay {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    std::wstring text;
    int durationMs = 2000;
    int position = 0;
    int icon = 0;
    int anim = 0;
    UINT dpi = 96;
    float dpiScale = 1.0f;
    float widthDip = 0.0f;
    float heightDip = 0.0f;
    int targetX = 0;
    int targetY = 0;
    int slideRangePx = 0;
    uint64_t startTick = 0;
    bool timerPeriodSet = false;
    std::unique_ptr<ToastBitmap> bitmap;
};

} // namespace

struct OverlayService::Impl {
    enum class RequestType {
        CreateHostWindow,
        DestroyHostWindow,
        Invoke,
        ShowToast,
        DismissOwner,
        Stop
    };

    struct SyncResult {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        HWND hwnd = nullptr;
    };

    struct Request {
        RequestType type = RequestType::Stop;
        OverlayWindowHandler* handler = nullptr;
        OverlayWindowOptions options;
        SyncResult* sync = nullptr;
        std::function<void()> fn;
        HWND hwnd = nullptr;
        HWND owner = nullptr;
        std::wstring text;
        int durationMs = 0;
        int position = 0;
        int icon = 0;
        int anim = 0;
    };

    std::mutex mutex;
    std::condition_variable readyCv;
    std::queue<std::unique_ptr<Request>> requests;
    std::thread thread;
    DWORD threadId = 0;
    bool ready = false;
    bool stopping = false;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    std::unordered_map<HWND, std::unique_ptr<ToastOverlay>> toasts;
    std::unordered_map<HWND, HWND> ownerToast;

    HWND CreateHostWindowSync(OverlayWindowHandler* handler,
                              const OverlayWindowOptions& options) {
        if (!handler) return nullptr;
        EnsureThread();
        if (GetCurrentThreadId() == threadId) {
            return CreateHostWindow(handler, options);
        }

        SyncResult sync;
        auto request = std::make_unique<Request>();
        request->type = RequestType::CreateHostWindow;
        request->handler = handler;
        request->options = options;
        request->sync = &sync;
        Post(std::move(request));

        std::unique_lock<std::mutex> lock(sync.mutex);
        sync.cv.wait(lock, [&sync]() { return sync.done; });
        return sync.hwnd;
    }

    void DestroyHostWindowSync(HWND hwnd) {
        if (!hwnd) return;
        EnsureThread();
        if (GetCurrentThreadId() == threadId) {
            DestroyHostWindow(hwnd);
            return;
        }

        SyncResult sync;
        auto request = std::make_unique<Request>();
        request->type = RequestType::DestroyHostWindow;
        request->hwnd = hwnd;
        request->sync = &sync;
        Post(std::move(request));

        std::unique_lock<std::mutex> lock(sync.mutex);
        sync.cv.wait(lock, [&sync]() { return sync.done; });
    }

    void InvokeSync(std::function<void()> fn) {
        if (!fn) return;
        EnsureThread();
        if (GetCurrentThreadId() == threadId) {
            fn();
            return;
        }

        SyncResult sync;
        auto request = std::make_unique<Request>();
        request->type = RequestType::Invoke;
        request->fn = std::move(fn);
        request->sync = &sync;
        Post(std::move(request));

        std::unique_lock<std::mutex> lock(sync.mutex);
        sync.cv.wait(lock, [&sync]() { return sync.done; });
    }

    void EnsureThread() {
        std::unique_lock<std::mutex> lock(mutex);
        if (thread.joinable() && ready && !stopping) {
            return;
        }
        if (thread.joinable()) {
            lock.unlock();
            thread.join();
            lock.lock();
        }
        ready = false;
        stopping = false;
        thread = std::thread([this]() { ThreadMain(); });
        readyCv.wait(lock, [this]() { return ready; });
    }

    void Post(std::unique_ptr<Request> request) {
        EnsureThread();
        DWORD tid = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            tid = threadId;
            requests.push(std::move(request));
        }
        if (tid) {
            PostThreadMessageW(tid, kOverlayWakeMessage, 0, 0);
        }
    }

    void Stop() {
        std::thread oldThread;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!thread.joinable()) {
                return;
            }
        }

        auto request = std::make_unique<Request>();
        request->type = RequestType::Stop;
        Post(std::move(request));

        {
            std::lock_guard<std::mutex> lock(mutex);
            oldThread = std::move(thread);
        }
        if (oldThread.joinable()) {
            oldThread.join();
        }
    }

    void ThreadMain() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        threadId = GetCurrentThreadId();
        MSG msg{};
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ToastWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        wc.lpszClassName = L"UiCore_OverlayToast";
        RegisterClassExW(&wc);

        WNDCLASSEXW hostClass{};
        hostClass.cbSize = sizeof(hostClass);
        hostClass.style = CS_HREDRAW | CS_VREDRAW;
        hostClass.lpfnWndProc = HostWndProc;
        hostClass.hInstance = GetModuleHandleW(nullptr);
        hostClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        hostClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        hostClass.lpszClassName = L"UiCore_OverlayHost";
        RegisterClassExW(&hostClass);

        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf()));

        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = true;
        }
        readyCv.notify_all();

        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == kOverlayWakeMessage) {
                DrainRequests();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        DestroyAllToasts();
        dwriteFactory.Reset();
        d2dFactory.Reset();
        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = false;
            threadId = 0;
            stopping = false;
        }
        CoUninitialize();
    }

    void DrainRequests() {
        for (;;) {
            std::unique_ptr<Request> request;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (requests.empty()) break;
                request = std::move(requests.front());
                requests.pop();
            }
            if (!request) continue;
            switch (request->type) {
            case RequestType::CreateHostWindow: {
                HWND hwnd = CreateHostWindow(request->handler, request->options);
                CompleteSync(request->sync, hwnd);
                break;
            }
            case RequestType::DestroyHostWindow:
                DestroyHostWindow(request->hwnd);
                CompleteSync(request->sync, nullptr);
                break;
            case RequestType::Invoke:
                if (request->fn) {
                    request->fn();
                }
                CompleteSync(request->sync, nullptr);
                break;
            case RequestType::ShowToast:
                CreateToast(*request);
                break;
            case RequestType::DismissOwner:
                DestroyToastForOwner(request->owner);
                break;
            case RequestType::Stop:
                stopping = true;
                DestroyAllToasts();
                PostQuitMessage(0);
                break;
            }
        }
    }

    void CompleteSync(SyncResult* sync, HWND hwnd) {
        if (!sync) return;
        {
            std::lock_guard<std::mutex> lock(sync->mutex);
            sync->hwnd = hwnd;
            sync->done = true;
        }
        sync->cv.notify_one();
    }

    HWND CreateHostWindow(OverlayWindowHandler* handler,
                          const OverlayWindowOptions& options) {
        if (!handler) return nullptr;
        return CreateWindowExW(
            options.exStyle,
            L"UiCore_OverlayHost",
            options.title.empty() ? L"" : options.title.c_str(),
            options.style,
            options.x, options.y,
            std::max(1, options.width), std::max(1, options.height),
            options.owner, nullptr, GetModuleHandleW(nullptr), handler);
    }

    void DestroyHostWindow(HWND hwnd) {
        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
    }

    void CreateToast(const Request& request) {
        if (!request.owner || !IsWindow(request.owner) || request.text.empty()) {
            return;
        }
        DestroyToastForOwner(request.owner);

        auto toast = std::make_unique<ToastOverlay>();
        toast->owner = request.owner;
        toast->text = request.text;
        toast->durationMs = std::max(1, request.durationMs);
        toast->position = request.position;
        toast->icon = request.icon;
        toast->anim = request.anim;
        toast->dpi = OverlayDpiForWindow(request.owner);
        toast->dpiScale = static_cast<float>(toast->dpi) / 96.0f;

        if (!MeasureToast(*toast) || !RenderToastBitmap(*toast)) {
            return;
        }

        const int widthPx = std::max(1, static_cast<int>(toast->widthDip * toast->dpiScale + 0.5f));
        const int heightPx = std::max(1, static_cast<int>(toast->heightDip * toast->dpiScale + 0.5f));
        const int startY = (toast->anim == 1)
                               ? toast->targetY
                               : toast->targetY + toast->slideRangePx;

        ToastOverlay* raw = toast.get();
        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            L"UiCore_OverlayToast", L"", WS_POPUP,
            toast->targetX, startY, widthPx, heightPx,
            request.owner, nullptr, GetModuleHandleW(nullptr), raw);
        if (!hwnd) {
            return;
        }

        raw->hwnd = hwnd;
        raw->startTick = GetTickCount64();
        timeBeginPeriod(1);
        raw->timerPeriodSet = true;
        SetTimer(hwnd, kToastTimerId, kToastTimerIntervalMs, nullptr);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        ownerToast[request.owner] = hwnd;
        toasts[hwnd] = std::move(toast);
        UpdateToastFrame(*raw);
    }

    bool MeasureToast(ToastOverlay& toast) {
        RECT client{};
        if (!GetClientRect(toast.owner, &client)) {
            return false;
        }
        POINT origin{0, 0};
        ClientToScreen(toast.owner, &origin);
        const float clientW = std::max(1, static_cast<int>(client.right - client.left)) / toast.dpiScale;
        const float clientH = std::max(1, static_cast<int>(client.bottom - client.top)) / toast.dpiScale;

        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        if (!dwriteFactory ||
            FAILED(dwriteFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                                                   DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL,
                                                   theme::kFontSizeNormal,
                                                   L"zh-cn", format.GetAddressOf()))) {
            return false;
        }
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const float iconSize = theme::kFontSizeNormal + 2.0f;
        const float iconGap = (toast.icon > 0) ? 8.0f : 0.0f;
        const float iconSpace = (toast.icon > 0) ? (iconSize + iconGap) : 0.0f;
        const float padH = 20.0f;
        const float padV = 10.0f;
        const float maxTextW = std::max(120.0f, std::min(620.0f, clientW - padH * 2.0f - iconSpace - 40.0f));

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory->CreateTextLayout(
                toast.text.c_str(), static_cast<UINT32>(toast.text.size()),
                format.Get(), maxTextW, 80.0f, layout.GetAddressOf()))) {
            return false;
        }
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);

        const float textW = std::min(maxTextW, std::max(1.0f, metrics.widthIncludingTrailingWhitespace));
        const float textH = std::max(theme::kFontSizeNormal, metrics.height);
        toast.widthDip = std::max(96.0f, textW + iconSpace + padH * 2.0f);
        toast.heightDip = std::max(iconSize, textH) + padV * 2.0f;

        const float xDip = (clientW - toast.widthDip) * 0.5f;
        float yDip = 50.0f;
        float hideOffsetDip = -(toast.heightDip + 60.0f);
        if (toast.position == 1) {
            yDip = (clientH - toast.heightDip) * 0.5f;
            hideOffsetDip = 0.0f;
        } else if (toast.position == 2) {
            yDip = clientH - toast.heightDip - 60.0f;
            hideOffsetDip = toast.heightDip + 60.0f;
        }

        toast.targetX = origin.x + static_cast<int>(xDip * toast.dpiScale);
        toast.targetY = origin.y + static_cast<int>(yDip * toast.dpiScale);
        toast.slideRangePx = static_cast<int>(hideOffsetDip * toast.dpiScale);
        return true;
    }

    bool RenderToastBitmap(ToastOverlay& toast) {
        const int widthPx = std::max(1, static_cast<int>(toast.widthDip * toast.dpiScale + 0.5f));
        const int heightPx = std::max(1, static_cast<int>(toast.heightDip * toast.dpiScale + 0.5f));

        auto bitmap = std::make_unique<ToastBitmap>();
        bitmap->width = widthPx;
        bitmap->height = heightPx;
        bitmap->dc = CreateCompatibleDC(nullptr);
        if (!bitmap->dc) {
            return false;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = widthPx;
        bmi.bmiHeader.biHeight = -heightPx;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap->bitmap = CreateDIBSection(bitmap->dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap->bitmap || !bits) {
            return false;
        }
        bitmap->oldBitmap = SelectObject(bitmap->dc, bitmap->bitmap);

        Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<FLOAT>(toast.dpi), static_cast<FLOAT>(toast.dpi));
        if (!d2dFactory || FAILED(d2dFactory->CreateDCRenderTarget(&props, target.GetAddressOf()))) {
            return false;
        }
        RECT rc{0, 0, widthPx, heightPx};
        if (FAILED(target->BindDC(bitmap->dc, &rc))) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
        target->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.18f, 0.92f), bgBrush.GetAddressOf());
        target->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.97f, 1.0f), textBrush.GetAddressOf());

        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        if (!dwriteFactory ||
            FAILED(dwriteFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                                                   DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL,
                                                   theme::kFontSizeNormal,
                                                   L"zh-cn", format.GetAddressOf()))) {
            return false;
        }
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        const D2D1_RECT_F box = D2D1::RectF(0.0f, 0.0f, toast.widthDip, toast.heightDip);
        target->FillRoundedRectangle(D2D1::RoundedRect(box, 8.0f, 8.0f), bgBrush.Get());

        const float fontSize = theme::kFontSizeNormal;
        const float iconSize = fontSize + 2.0f;
        const float iconGap = (toast.icon > 0) ? 8.0f : 0.0f;
        const float iconSpace = (toast.icon > 0) ? (iconSize + iconGap) : 0.0f;
        const float padH = 20.0f;

        if (toast.icon > 0) {
            DrawIcon(*target.Get(), toast);
        }

        const D2D1_RECT_F textRect = D2D1::RectF(
            padH + iconSpace, 0.0f,
            toast.widthDip - padH, toast.heightDip);
        target->DrawTextW(toast.text.c_str(), static_cast<UINT32>(toast.text.size()),
                          format.Get(), textRect, textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

        HRESULT hr = target->EndDraw();
        if (FAILED(hr)) {
            return false;
        }

        toast.bitmap = std::move(bitmap);
        return true;
    }

    static void DrawIcon(ID2D1DCRenderTarget& target, const ToastOverlay& toast) {
        const float fontSize = theme::kFontSizeNormal;
        const float iconSize = fontSize + 2.0f;
        const float padH = 20.0f;
        const float ix = padH;
        const float iy = (toast.heightDip - iconSize) * 0.5f;
        const float cx = ix + iconSize * 0.5f;
        const float cy = iy + iconSize * 0.5f;
        const float r = iconSize * 0.5f;

        D2D1_COLOR_F color = D2D1::ColorF(0.3f, 0.85f, 0.4f, 1.0f);
        if (toast.icon == 2) {
            color = D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f);
        } else if (toast.icon == 3) {
            color = D2D1::ColorF(1.0f, 0.82f, 0.2f, 1.0f);
        }

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        target.CreateSolidColorBrush(color, brush.GetAddressOf());
        if (!brush) return;

        if (toast.icon == 1) {
            target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush.Get(), 1.5f);
            target.DrawLine(D2D1::Point2F(cx - r * 0.32f, cy + r * 0.05f),
                            D2D1::Point2F(cx - r * 0.02f, cy + r * 0.35f),
                            brush.Get(), 2.0f);
            target.DrawLine(D2D1::Point2F(cx - r * 0.02f, cy + r * 0.35f),
                            D2D1::Point2F(cx + r * 0.42f, cy - r * 0.26f),
                            brush.Get(), 2.0f);
        } else if (toast.icon == 2) {
            target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush.Get(), 1.5f);
            const float d = r * 0.34f;
            target.DrawLine(D2D1::Point2F(cx - d, cy - d), D2D1::Point2F(cx + d, cy + d),
                            brush.Get(), 2.0f);
            target.DrawLine(D2D1::Point2F(cx + d, cy - d), D2D1::Point2F(cx - d, cy + d),
                            brush.Get(), 2.0f);
        } else if (toast.icon == 3) {
            target.DrawLine(D2D1::Point2F(cx, cy - r * 0.55f),
                            D2D1::Point2F(cx - r * 0.55f, cy + r * 0.42f),
                            brush.Get(), 1.8f);
            target.DrawLine(D2D1::Point2F(cx - r * 0.55f, cy + r * 0.42f),
                            D2D1::Point2F(cx + r * 0.55f, cy + r * 0.42f),
                            brush.Get(), 1.8f);
            target.DrawLine(D2D1::Point2F(cx + r * 0.55f, cy + r * 0.42f),
                            D2D1::Point2F(cx, cy - r * 0.55f),
                            brush.Get(), 1.8f);
            target.DrawLine(D2D1::Point2F(cx, cy - r * 0.15f),
                            D2D1::Point2F(cx, cy + r * 0.16f),
                            brush.Get(), 2.0f);
            target.DrawLine(D2D1::Point2F(cx, cy + r * 0.30f),
                            D2D1::Point2F(cx + 0.1f, cy + r * 0.31f),
                            brush.Get(), 2.0f);
        }
    }

    void UpdateToastFrame(ToastOverlay& toast) {
        if (!toast.hwnd || !toast.bitmap || !toast.bitmap->dc) {
            return;
        }

        const uint64_t now = GetTickCount64();
        const uint64_t elapsed = (toast.startTick != 0 && now >= toast.startTick)
                                     ? (now - toast.startTick) : 0;
        const uint64_t inEnd = kToastInMs;
        const uint64_t holdEnd = inEnd + static_cast<uint64_t>(toast.durationMs);
        const uint64_t outEnd = holdEnd + kToastOutMs;

        float alpha = 1.0f;
        float slide = 1.0f;
        if (elapsed < inEnd) {
            const float t = EaseOutCubic(static_cast<float>(elapsed) / static_cast<float>(kToastInMs));
            slide = t;
            alpha = (toast.anim == 1) ? t : 1.0f;
        } else if (elapsed < holdEnd) {
            slide = 1.0f;
            alpha = 1.0f;
        } else if (elapsed < outEnd) {
            const float t = EaseOutCubic(static_cast<float>(elapsed - holdEnd) / static_cast<float>(kToastOutMs));
            slide = 1.0f - t;
            alpha = 1.0f - t;
        } else {
            DestroyToastByHwnd(toast.hwnd);
            return;
        }

        const int y = (toast.anim == 1)
                          ? toast.targetY
                          : toast.targetY + static_cast<int>(toast.slideRangePx * (1.0f - slide));
        const BYTE alphaByte = static_cast<BYTE>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        POINT dst{toast.targetX, y};
        POINT src{0, 0};
        SIZE size{toast.bitmap->width, toast.bitmap->height};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = alphaByte;
        blend.AlphaFormat = AC_SRC_ALPHA;
        UpdateLayeredWindow(toast.hwnd, nullptr, &dst, &size, toast.bitmap->dc, &src,
                            0, &blend, ULW_ALPHA);
    }

    void DestroyToastForOwner(HWND owner) {
        auto it = ownerToast.find(owner);
        if (it == ownerToast.end()) {
            return;
        }
        DestroyToastByHwnd(it->second);
    }

    void DestroyToastByHwnd(HWND hwnd) {
        auto it = toasts.find(hwnd);
        if (it == toasts.end()) {
            return;
        }
        ToastOverlay* toast = it->second.get();
        if (toast->timerPeriodSet) {
            timeEndPeriod(1);
            toast->timerPeriodSet = false;
        }
        if (toast->hwnd && IsWindow(toast->hwnd)) {
            KillTimer(toast->hwnd, kToastTimerId);
            DestroyWindow(toast->hwnd);
        }
        ownerToast.erase(toast->owner);
        toasts.erase(it);
    }

    void DestroyAllToasts() {
        while (!toasts.empty()) {
            DestroyToastByHwnd(toasts.begin()->first);
        }
        ownerToast.clear();
    }

    static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        OverlayWindowHandler* handler = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            handler = static_cast<OverlayWindowHandler*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(handler));
        } else {
            handler = reinterpret_cast<OverlayWindowHandler*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (handler) {
            bool handled = false;
            LRESULT result = handler->HandleOverlayMessage(hwnd, msg, wParam, lParam, handled);
            if (handled) {
                return result;
            }
        }

        if (msg == WM_NCDESTROY) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK ToastWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        ToastOverlay* toast = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            toast = static_cast<ToastOverlay*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(toast));
        } else {
            toast = reinterpret_cast<ToastOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
        case WM_TIMER:
            if (wParam == kToastTimerId && toast) {
                OverlayService::Instance().impl_->UpdateToastFrame(*toast);
                return 0;
            }
            break;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_NCCALCSIZE:
            if (wParam) return 0;
            break;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

OverlayService& OverlayService::Instance() {
    static OverlayService service;
    return service;
}

OverlayService::OverlayService()
    : impl_(new Impl()) {
}

OverlayService::~OverlayService() {
    Stop();
    delete impl_;
}

HWND OverlayService::CreateHostWindowSync(OverlayWindowHandler* handler,
                                          const OverlayWindowOptions& options) {
    return impl_->CreateHostWindowSync(handler, options);
}

void OverlayService::DestroyHostWindowSync(HWND hwnd) {
    impl_->DestroyHostWindowSync(hwnd);
}

void OverlayService::InvokeSync(std::function<void()> fn) {
    impl_->InvokeSync(std::move(fn));
}

void OverlayService::ShowToast(HWND owner, const std::wstring& text, int durationMs,
                               int position, int icon, int anim) {
    auto request = std::make_unique<Impl::Request>();
    request->type = Impl::RequestType::ShowToast;
    request->owner = owner;
    request->text = text;
    request->durationMs = durationMs;
    request->position = position;
    request->icon = icon;
    request->anim = anim;
    impl_->Post(std::move(request));
}

void OverlayService::DismissOwner(HWND owner) {
    auto request = std::make_unique<Impl::Request>();
    request->type = Impl::RequestType::DismissOwner;
    request->owner = owner;
    impl_->Post(std::move(request));
}

void OverlayService::Stop() {
    impl_->Stop();
}

} // namespace ui

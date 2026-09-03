#pragma once

#include <windows.h>
#include <functional>
#include <string>

namespace ui {

class OverlayWindowHandler {
public:
    virtual ~OverlayWindowHandler() = default;
    virtual LRESULT HandleOverlayMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, bool& handled) = 0;
};

struct OverlayWindowOptions {
    HWND owner = nullptr;
    DWORD exStyle = 0;
    DWORD style = WS_POPUP;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    std::wstring title;
};

class OverlayService {
public:
    static OverlayService& Instance();

    HWND CreateHostWindowSync(OverlayWindowHandler* handler,
                              const OverlayWindowOptions& options);
    void DestroyHostWindowSync(HWND hwnd);
    void InvokeSync(std::function<void()> fn);

    void ShowToast(HWND owner, const std::wstring& text, int durationMs,
                   int position, int icon, int anim);
    void DismissOwner(HWND owner);
    void Stop();

private:
    OverlayService();
    ~OverlayService();
    OverlayService(const OverlayService&) = delete;
    OverlayService& operator=(const OverlayService&) = delete;

    struct Impl;
    Impl* impl_;
};

} // namespace ui

// 系统托盘：图标 / 提示 / 菜单 / 命令分派 / 还原主窗口（自 main.cpp 抽离）。
#include "tray.h"
#include "data.h"
#include "autostart.h"
#include "i18n.h"
#include <shellapi.h>

static UiWindow g_win = 0;
static HWND     g_hwnd = nullptr;
static NOTIFYICONDATAW g_nid;
static HMENU   g_trayMenu = nullptr;

void TrayInit(UiWindow win, HWND hwnd) { g_win = win; g_hwnd = hwnd; }

static HICON CreateAppIcon() {
    const int S = 32;
    HDC hdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP clr = CreateCompatibleBitmap(hdc, S, S);
    HBITMAP msk = CreateBitmap(S, S, 1, 1, nullptr);
    HGDIOBJ oc = SelectObject(mem, clr);
    RECT rc = {0, 0, S, S};
    HBRUSH bg = CreateSolidBrush(RGB(38, 102, 236));
    FillRect(mem, &rc, bg); DeleteObject(bg);
    HBRUSH w = CreateSolidBrush(RGB(255, 255, 255));
    RECT k1 = {6, 7, 26, 15}; RECT k2 = {6, 18, 26, 26};
    FillRect(mem, &k1, w); FillRect(mem, &k2, w); DeleteObject(w);
    HBRUSH g2 = CreateSolidBrush(RGB(86, 204, 130));
    RECT g = {6, 19, 9, 23}; FillRect(mem, &g, g2); DeleteObject(g2);
    SelectObject(mem, oc);
    ICONINFO ii = {};
    ii.fIcon = TRUE; ii.hbmColor = clr; ii.hbmMask = msk;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(clr); DeleteObject(msk);
    DeleteDC(mem); ReleaseDC(nullptr, hdc);
    return icon;
}

void TrayRefreshCheck() {
    if (g_trayMenu) {
        CheckMenuItem(g_trayMenu, IDM_AUTOSTART, MF_BYCOMMAND | (IsAutoStart() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(g_trayMenu, IDM_PAUSE, MF_BYCOMMAND | (app().paused ? MF_CHECKED : MF_UNCHECKED));
    }
}

void TrayShowWindow() {
    if (!g_win) return;
    if (g_hwnd) {
        // 从托盘直接还原为正常可见窗口：
        // - 若窗口仍处于最小化(IsIconic)，先 SW_RESTORE 真正还原，避免恢复后
        // 虽可见却仍是“最小化状态”；
        // - 再用“立即显示、无开场动画”路径(ShowImmediate)出图并激活，避免
        // ui_window_show 触发的 StartWindowOpenAnimation（滑入淡入）造成“闪烁/重现”。
        if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
        ui_window_show_immediate(g_win);
        SetForegroundWindow(g_hwnd);
    } else {
        ui_window_show_immediate(g_win);
    }
    app().needsRefresh = true;   // 恢复显示时置位，由 TI_POLL 补刷隐藏期间累计的数据
}

UINT TrayShowMenu() {
    // 每次弹出都按当前语言重建菜单（保证切换语言后菜单文案即时生效）
    if (g_trayMenu) { DestroyMenu(g_trayMenu); g_trayMenu = nullptr; }
    g_trayMenu = CreatePopupMenu();
    AppendMenuW(g_trayMenu, MF_STRING, IDM_SHOW, tr(L"显示主界面", L"Show Window"));
    AppendMenuW(g_trayMenu, MF_STRING, IDM_PAUSE, tr(L"暂停记录", L"Pause Recording"));
    AppendMenuW(g_trayMenu, MF_STRING, IDM_AUTOSTART, tr(L"开机自启动", L"Start on Boot"));
    AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_trayMenu, MF_STRING, IDM_EXIT, tr(L"退出", L"Exit"));
    TrayRefreshCheck();
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    return (UINT)TrackPopupMenu(g_trayMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g_hwnd, nullptr);
}

void TrayAddIcon() {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = kTrayMsg;
    g_nid.hIcon = CreateAppIcon();
    wcscpy_s(g_nid.szTip, tr(L"键鼠使用记录", L"KeyMouseTracker"));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void TrayUpdate(bool paused) {
    wcscpy_s(g_nid.szTip, paused
        ? tr(L"键鼠使用记录（已暂停）", L"KeyMouseTracker (paused)")
        : tr(L"键鼠使用记录", L"KeyMouseTracker"));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void TrayHandleCommand(UINT cmd) {
    if (cmd == IDM_SHOW) TrayShowWindow();
    else if (cmd == IDM_PAUSE) {
        app().paused = !app().paused;
        app().needsRefresh = true;
        TrayUpdate(app().paused);
        TrayRefreshCheck();
    } else if (cmd == IDM_AUTOSTART) {
        SetAutoStart(!IsAutoStart());
        app().needsRefresh = true;
        TrayRefreshCheck();
    } else if (cmd == IDM_EXIT) {
        ui_quit(0);
    }
}

void TrayCleanup() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_trayMenu) { DestroyMenu(g_trayMenu); g_trayMenu = nullptr; }
}

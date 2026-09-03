// 主程序（core-ui 版）：现代界面 + 全局键鼠钩子 + 系统托盘 + 持久化
#include <ui_core.h>
#include "data.h"
#include "hooks.h"
#include "autostart.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "app_uix.embed.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static UiPage  g_page = 0;
static UiWindow g_win = 0;
static HWND    g_hwnd = nullptr;
static NOTIFYICONDATAW g_nid;
static HMENU   g_trayMenu = nullptr;
static const UINT WM_TRAY = WM_APP + 1;
static const UINT_PTR kSubclassId = 0x4B4D54; // "KMT"

// 定时器
enum { TI_SAMPLE = 1, TI_SAVE = 2, TI_ACTIVE = 3, TI_POLL = 4 };
static const UINT kSampleMs = 33;
static const UINT kSaveMs = 30000;

enum { IDM_SHOW = 1000, IDM_PAUSE = 1001, IDM_AUTOSTART = 1002, IDM_EXIT = 1003 };

static int g_lastPauseCmd = 0;
static int g_lastAutoCmd = 0;
static int g_lastExitCmd = 0;
static int g_lastThemeCmd = 0;
static int g_lastExportCmd = 0;
static int g_lastImportCmd = 0;
static int g_lastClearCmd = 0;
static int g_lastLinkCmd = 0;

static int g_trendMode = 0;
static int g_trendSelY = 0, g_trendSelM = 0, g_trendSelD = 0;

static float g_khOx = 0, g_khOy = 0, g_khUnit = 0;
// 键间距（px），随 unit 同比缩放；绘制时更新成缩放后的值，命中测试复用同一值
static float g_khGap = 5.0f;
static float g_mhOx = 0, g_mhOy = 0, g_mhCw = 0, g_mhCh = 0;
static bool g_mhValid = false;
// 鼠标热力悬浮状态：命中格坐标 + 悬停指针的 widget-local 坐标（用于直绘计数覆层）
static int g_mhHoverGx = -1, g_mhHoverGy = -1;
static float g_mhHoverX = 0, g_mhHoverY = 0;
// 趋势图悬浮状态：命中柱下标 + 悬停指针坐标（用于直绘数值浮窗）；几何由绘制侧缓存
static int g_trHover = -1;
static float g_trHoverX = 0, g_trHoverY = 0;
static float g_trBaseX = 0, g_trSlot = 0;   // 柱区起始 x 与单柱槽宽
static float g_trTopY = 0, g_trBotY = 0;    // 绘图区上下边界（y 越界即收起浮窗）
static size_t g_trN = 0;

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

static void refreshTrayCheck() {
    if (g_trayMenu) {
        CheckMenuItem(g_trayMenu, IDM_AUTOSTART, MF_BYCOMMAND | (IsAutoStart() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(g_trayMenu, IDM_PAUSE, MF_BYCOMMAND | (app().paused ? MF_CHECKED : MF_UNCHECKED));
    }
}

static void ShowMainWindow() {
    if (!g_win) return;
    if (g_hwnd) {
        // 从托盘直接还原为正常可见窗口：
        // - 若窗口仍处于最小化(IsIconic)，先 SW_RESTORE 真正还原，避免恢复后
        //   虽可见却仍是“最小化状态”；
        // - 再用“立即显示、无开场动画”路径(ShowImmediate)出图并激活，避免
        //   ui_window_show 触发的 StartWindowOpenAnimation（滑入淡入）造成“闪烁/重现”。
        if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
        ui_window_show_immediate(g_win);
        SetForegroundWindow(g_hwnd);
    } else {
        ui_window_show_immediate(g_win);
    }
}

static UINT ShowTrayMenu() {
    if (!g_trayMenu) {
        g_trayMenu = CreatePopupMenu();
        AppendMenuW(g_trayMenu, MF_STRING, IDM_SHOW, L"显示主界面");
        AppendMenuW(g_trayMenu, MF_STRING, IDM_PAUSE, L"暂停记录");
        AppendMenuW(g_trayMenu, MF_STRING, IDM_AUTOSTART, L"开机自启动");
        AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(g_trayMenu, MF_STRING, IDM_EXIT, L"退出");
    }
    refreshTrayCheck();
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    return (UINT)TrackPopupMenu(g_trayMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g_hwnd, nullptr);
}

static void AddTrayIcon() {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = CreateAppIcon();
    wcscpy_s(g_nid.szTip, L"键鼠使用记录");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}
static void UpdateTray(bool paused) {
    wcscpy_s(g_nid.szTip, paused ? L"键鼠使用记录（已暂停）" : L"键鼠使用记录");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void HandleTrayCommand(UINT cmd) {
    if (cmd == IDM_SHOW) ShowMainWindow();
    else if (cmd == IDM_PAUSE) {
        app().paused = !app().paused;
        app().needsRefresh = true;
        UpdateTray(app().paused);
        refreshTrayCheck();
    } else if (cmd == IDM_AUTOSTART) {
        SetAutoStart(!IsAutoStart());
        app().needsRefresh = true;
        refreshTrayCheck();
    } else if (cmd == IDM_EXIT) {
        ui_quit(0);
    }
}

static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR idSubclass, DWORD_PTR) {
    if (idSubclass == kSubclassId) {
        if (msg == WM_SIZE && wp == SIZE_MINIMIZED) {
            if (g_win) ui_window_hide(g_win);
            else ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (msg == WM_TRAY) {
            UINT m = (UINT)lp;
            if (LOWORD(m) == WM_RBUTTONUP || LOWORD(m) == WM_CONTEXTMENU) {
                HandleTrayCommand(ShowTrayMenu());
            } else if (LOWORD(m) == WM_LBUTTONDBLCLK) {
                ShowMainWindow();
            }
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static int OnCloseRequest(UiWindow win, void*) {
    ui_window_hide(win);
    return 0;
}

static void OnWindowResize(UiWindow, int w, int, void*) {
    if (g_page) ui_page_set_bool(g_page, "compact", w < 640 ? 1 : 0);
}

static const char* vkLabel(uint8_t vk, char buf[32]) {
    if (vk >= 'A' && vk <= 'Z') { buf[0] = vk; buf[1] = 0; return buf; }
    if (vk >= '0' && vk <= '9') { buf[0] = vk; buf[1] = 0; return buf; }
    switch (vk) {
        case VK_SPACE: return "Space"; case VK_BACK: return "Back"; case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter"; case VK_CAPITAL: return "Caps"; case VK_SHIFT: return "Shift";
        case VK_LSHIFT: return "LShift"; case VK_RSHIFT: return "RShift";
        case VK_CONTROL: return "Ctrl"; case VK_LCONTROL: return "LCtrl"; case VK_RCONTROL: return "RCtrl";
        case VK_MENU: return "Alt"; case VK_LMENU: return "LAlt"; case VK_RMENU: return "RAlt";
        case VK_ESCAPE: return "Esc"; case VK_DELETE: return "Del"; case VK_INSERT: return "Ins";
        case VK_HOME: return "Home"; case VK_END: return "End"; case VK_PRIOR: return "PgUp";
        case VK_NEXT: return "PgDn";
        case VK_LEFT: return "\xe2\x86\x90"; case VK_RIGHT: return "\xe2\x86\x92";
        case VK_UP: return "\xe2\x86\x91"; case VK_DOWN: return "\xe2\x86\x93";
        case VK_LWIN: return "Win"; case VK_RWIN: return "Win"; case VK_APPS: return "Menu";
        case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3"; case VK_F4: return "F4";
        case VK_F5: return "F5"; case VK_F6: return "F6"; case VK_F7: return "F7"; case VK_F8: return "F8";
        case VK_F9: return "F9"; case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
        case 0xBA: return ";"; case 0xBB: return "="; case 0xBC: return ","; case 0xBD: return "-";
        case 0xBE: return "."; case 0xBF: return "/"; case 0xC0: return "`"; case 0xDB: return "[";
        case 0xDC: return "\\"; case 0xDD: return "]"; case 0xDE: return "'";
        default:
            if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(buf, 32, "N%d", vk - VK_NUMPAD0); return buf; }
            snprintf(buf, 32, "%d", vk); return buf;
    }
}

static std::string jsonEsc(const char* s) {
    std::string out;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p == '\\') out += "\\\\";
        else if (*p == '"') out += "\\\"";
        else if (*p >= 0x20) out += (char)*p;
    }
    return out;
}
static int jsonInt(const char* json, int fallback) {
    if (!json) return fallback;
    while (*json && !((*json >= '0' && *json <= '9') || *json == '-')) ++json;
    if (!*json) return fallback;
    return (int)strtod(json, nullptr);
}

static std::string buildStatsJson() {
    ensureCurDay();
    auto& days = app().days;
    auto it = days.find(app().cur);

    uint64_t tKeys = 0, tClicks = 0, tMotion = 0;
    uint32_t tML = 0, tMR = 0, tMM = 0;
    if (it != days.end()) {
        const DayData& d = it->second;
        tKeys = d.keys; tClicks = d.clicks; tMotion = d.motion;
        tML = d.mLeft; tMR = d.mRight; tMM = d.mMid;
    }

    uint64_t totKeys = 0, totClicks = 0, totActive = 0, totMotion = 0;
    uint64_t aL = 0, aR = 0, aM = 0;
    for (auto& kv : days) {
        const DayData& d = kv.second;
        totKeys += d.keys;
        totClicks += d.clicks;
        totActive += d.activeSec;
        totMotion += d.motion;
        aL += d.mLeft; aR += d.mRight; aM += d.mMid;
    }
    int totDays = (int)days.size();

    const auto& kc = cumulativeKeys();
    std::vector<std::pair<uint8_t, uint32_t>> sorted(kc.begin(), kc.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<uint8_t, uint32_t>& a, const std::pair<uint8_t, uint32_t>& b) {
                  return a.second > b.second;
              });
    std::string table = "[";
    char lbuf[32];
    for (size_t i = 0; i < sorted.size(); ++i) {
        const char* l = vkLabel(sorted[i].first, lbuf);
        if (i) table += ",";
        table += "{\"l\":\"" + jsonEsc(l) + "\",\"c\":" + std::to_string(sorted[i].second) + "}";
    }
    table += "]";

    std::string out;
    out += "{\"paused\":" + std::string(app().paused ? "true" : "false");
    out += ",\"autostart\":" + std::string(IsAutoStart() ? "true" : "false");
    out += ",\"today\":{\"keys\":" + std::to_string(tKeys) + ",\"clicks\":" + std::to_string(tClicks) +
           ",\"motion\":" + std::to_string(tMotion) + "}";
    out += ",\"total\":{\"keys\":" + std::to_string(totKeys) + ",\"clicks\":" + std::to_string(totClicks) +
           ",\"days\":" + std::to_string(totDays) + ",\"activeSec\":" + std::to_string(totActive) + "}";
    out += ",\"mouseToday\":{\"left\":" + std::to_string(tML) + ",\"right\":" + std::to_string(tMR) +
           ",\"mid\":" + std::to_string(tMM) + ",\"total\":" + std::to_string((uint64_t)tML + tMR + tMM) + "}";
    out += ",\"mouseAll\":{\"left\":" + std::to_string(aL) + ",\"right\":" + std::to_string(aR) +
           ",\"mid\":" + std::to_string(aM) + ",\"total\":" + std::to_string(aL + aR + aM) +
           ",\"motion\":" + std::to_string(totMotion) + "}";
    out += ",\"keyTable\":" + table;
    out += "}";
    return out;
}

static void pushStats() {
    if (!g_page) return;
    std::string json = buildStatsJson();
    ui_page_set_json(g_page, "stats", json.c_str());
    if (g_win) ui_window_invalidate(g_win);
}

// 键位定义：zone 分区（0=主键区 1=编辑键区 2=小键盘区），x/y 为分区内网格坐标，
// 单位 = 1 个标准键宽。分区间距按"普通键间距的 2 倍"动态计算（见 zoneOffset）。
struct KeyCell { const wchar_t* l; uint8_t vk; uint8_t zone; float x, y, w, h; };
static const KeyCell* g_khDragLast = nullptr;   // 左键拖拽反转：上一命中的键（同一次拖拽路径内每键只切换一次）

// 主键区宽 15u，编辑键区宽 3u，小键盘区宽 4u
static const float kZoneUnits[3] = { 15.0f, 3.0f, 4.0f };
static const float kBoardMaxY = 6.0f;  

// 分区起点像素偏移：分区间距 = 2×普通键间距（gap 为普通键之间的视觉缝隙）
static float zoneOffset(int zone, float unit, float gap) {
    float off = 0;
    for (int i = 0; i < zone; ++i) off += kZoneUnits[i] * unit + 2.0f * gap;
    return off;
}

// ---- 键盘配列（关于页设置，持久化）----
// 0=108 全尺寸（三区全显） 1=87 TKL（去小键盘） 2=61 紧凑（仅主键区，
// 去 F1~F12 与 `，Esc 下移占据 ` 位 —— 与真实 60% 配列一致）
static int layoutZoneCount() { return app().kbLayout == 0 ? 3 : (app().kbLayout == 1 ? 2 : 1); }
static float layoutBoardMaxY() {
    // 61 键：去掉 F 行后内容只有 5 行（数字行 … 底行，坐标整体上移一行 0..4），
    // 板高用 5 才能让整块内容在画布内垂直居中；沿用 6 会多留一行空档偏上。
    return app().kbLayout == 2 ? 5.0f : kBoardMaxY;
}
static float layoutZoneUnitsSum() {
    float s = 0;
    for (int i = 0; i < layoutZoneCount(); ++i) s += kZoneUnits[i];
    return s;
}
static bool keyInLayout(const KeyCell& k) {
    if (app().kbLayout == 0) return true;
    if (k.zone >= layoutZoneCount()) return false;   // 87/61：无小键盘；61：无编辑区
    if (app().kbLayout == 2) {
        if (k.vk >= 112 && k.vk <= 123) return false;
        if (k.vk == 192) return false;               
    }
    return true;
}
// 累计数中该 vk 是否参与当前配列的色阶归一化（定义在 kKeys 之后）
static bool vkInLayout(uint8_t vk);

// 标准全尺寸键盘布局（108 键）。主键区各行总宽 15u 严格对齐。
static const KeyCell kKeys[] = {
   
    { L"Esc",  27, 0,  0.0f, 0, 1, 1 },
    { L"F1",  112, 0,  2.0f, 0, 1, 1 }, { L"F2", 113, 0,  3.0f, 0, 1, 1 },
    { L"F3",  114, 0,  4.0f, 0, 1, 1 }, { L"F4", 115, 0,  5.0f, 0, 1, 1 },
    { L"F5",  116, 0,  6.5f, 0, 1, 1 }, { L"F6", 117, 0,  7.5f, 0, 1, 1 },
    { L"F7",  118, 0,  8.5f, 0, 1, 1 }, { L"F8", 119, 0,  9.5f, 0, 1, 1 },
    { L"F9",  120, 0, 11.0f, 0, 1, 1 }, { L"F10", 121, 0, 12.0f, 0, 1, 1 },
    { L"F11", 122, 0, 13.0f, 0, 1, 1 }, { L"F12", 123, 0, 14.0f, 0, 1, 1 },

   
    { L"`", 192, 0, 0, 1, 1, 1 },
    { L"1", 49, 0, 1, 1, 1, 1 }, { L"2", 50, 0, 2, 1, 1, 1 }, { L"3", 51, 0, 3, 1, 1, 1 },
    { L"4", 52, 0, 4, 1, 1, 1 }, { L"5", 53, 0, 5, 1, 1, 1 }, { L"6", 54, 0, 6, 1, 1, 1 },
    { L"7", 55, 0, 7, 1, 1, 1 }, { L"8", 56, 0, 8, 1, 1, 1 }, { L"9", 57, 0, 9, 1, 1, 1 },
    { L"0", 48, 0, 10, 1, 1, 1 },
    { L"-", 189, 0, 11, 1, 1, 1 }, { L"=", 187, 0, 12, 1, 1, 1 },
    { L"Back", 8, 0, 13, 1, 2, 1 },

   
    { L"Tab", 9, 0, 0, 2, 1.5f, 1 },
    { L"Q", 81, 0, 1.5f, 2, 1, 1 }, { L"W", 87, 0, 2.5f, 2, 1, 1 }, { L"E", 69, 0, 3.5f, 2, 1, 1 },
    { L"R", 82, 0, 4.5f, 2, 1, 1 }, { L"T", 84, 0, 5.5f, 2, 1, 1 }, { L"Y", 89, 0, 6.5f, 2, 1, 1 },
    { L"U", 85, 0, 7.5f, 2, 1, 1 }, { L"I", 73, 0, 8.5f, 2, 1, 1 }, { L"O", 79, 0, 9.5f, 2, 1, 1 },
    { L"P", 80, 0, 10.5f, 2, 1, 1 },
    { L"[", 219, 0, 11.5f, 2, 1, 1 }, { L"]", 221, 0, 12.5f, 2, 1, 1 }, { L"\\", 220, 0, 13.5f, 2, 1.5f, 1 },

   
    { L"Caps", 20, 0, 0, 3, 1.75f, 1 },
    { L"A", 65, 0, 1.75f, 3, 1, 1 }, { L"S", 83, 0, 2.75f, 3, 1, 1 }, { L"D", 68, 0, 3.75f, 3, 1, 1 },
    { L"F", 70, 0, 4.75f, 3, 1, 1 }, { L"G", 71, 0, 5.75f, 3, 1, 1 }, { L"H", 72, 0, 6.75f, 3, 1, 1 },
    { L"J", 74, 0, 7.75f, 3, 1, 1 }, { L"K", 75, 0, 8.75f, 3, 1, 1 }, { L"L", 76, 0, 9.75f, 3, 1, 1 },
    { L";", 186, 0, 10.75f, 3, 1, 1 }, { L"'", 222, 0, 11.75f, 3, 1, 1 },
    { L"Enter", 13, 0, 12.75f, 3, 2.25f, 1 },

   
    { L"Shift", 160, 0, 0, 4, 2.25f, 1 },  
    { L"Z", 90, 0, 2.25f, 4, 1, 1 }, { L"X", 88, 0, 3.25f, 4, 1, 1 }, { L"C", 67, 0, 4.25f, 4, 1, 1 },
    { L"V", 86, 0, 5.25f, 4, 1, 1 }, { L"B", 66, 0, 6.25f, 4, 1, 1 }, { L"N", 78, 0, 7.25f, 4, 1, 1 },
    { L"M", 77, 0, 8.25f, 4, 1, 1 },
    { L",", 188, 0, 9.25f, 4, 1, 1 }, { L".", 190, 0, 10.25f, 4, 1, 1 }, { L"/", 191, 0, 11.25f, 4, 1, 1 },
    { L"Shift", 161, 0, 12.25f, 4, 2.75f, 1 },

   
    { L"Ctrl", 162, 0, 0, 5, 1.25f, 1 },    
    { L"Win", 91, 0, 1.25f, 5, 1.25f, 1 },
    { L"Alt", 164, 0, 2.5f, 5, 1.25f, 1 },  
    { L"", 32, 0, 3.75f, 5, 6.25f, 1 },     
    { L"Alt", 165, 0, 10.0f, 5, 1.25f, 1 }, 
    { L"Win", 92, 0, 11.25f, 5, 1.25f, 1 },
    { L"Menu", 93, 0, 12.5f, 5, 1.25f, 1 },
    { L"Ctrl", 163, 0, 13.75f, 5, 1.25f, 1 },// 右 Ctrl

   
    { L"PrtSc", 44, 1, 0, 0, 1, 1 }, { L"ScrLk", 145, 1, 1, 0, 1, 1 }, { L"Pause", 19, 1, 2, 0, 1, 1 },
    { L"Ins", 45, 1, 0, 1, 1, 1 },  { L"Home", 36, 1, 1, 1, 1, 1 },  { L"PgUp", 33, 1, 2, 1, 1, 1 },
    { L"Del", 46, 1, 0, 2, 1, 1 },  { L"End", 35, 1, 1, 2, 1, 1 },   { L"PgDn", 34, 1, 2, 2, 1, 1 },
    { L"\u2191", 38, 1, 1, 4, 1, 1 },  
    { L"\u2190", 37, 1, 0, 5, 1, 1 }, { L"\u2193", 40, 1, 1, 5, 1, 1 }, { L"\u2192", 39, 1, 2, 5, 1, 1 },

   
    { L"Num", 144, 2, 0, 1, 1, 1 },   
    { L"/", 111, 2, 1, 1, 1, 1 },     
    { L"*", 106, 2, 2, 1, 1, 1 },     
    { L"-", 109, 2, 3, 1, 1, 1 },     
    { L"7", 103, 2, 0, 2, 1, 1 }, { L"8", 104, 2, 1, 2, 1, 1 }, { L"9", 105, 2, 2, 2, 1, 1 },
    { L"+", 107, 2, 3, 2, 1, 2 },     
    { L"4", 100, 2, 0, 3, 1, 1 }, { L"5", 101, 2, 1, 3, 1, 1 }, { L"6", 102, 2, 2, 3, 1, 1 },
    { L"1", 97, 2, 0, 4, 1, 1 }, { L"2", 98, 2, 1, 4, 1, 1 }, { L"3", 99, 2, 2, 4, 1, 1 },
    { L"Enter", 13, 2, 3, 4, 1, 2 },  
    { L"0", 96, 2, 0, 5, 2, 1 },      
    { L".", 110, 2, 2, 5, 1, 1 },
};

static const int kNumKeys = (int)(sizeof(kKeys) / sizeof(KeyCell));

// vk 可能在布局表中多次出现（如主键区/小键盘区各有一个 Enter）——任一处
// 可见即参与当前配列的色阶归一化
static bool vkInLayout(uint8_t vk) {
    for (int i = 0; i < kNumKeys; ++i)
        if (kKeys[i].vk == vk && keyInLayout(kKeys[i])) return true;
    return false;
}

static UiColor rgb255(int r, int g, int b, int a = 255) {
    return UiColor{ r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
}
// 8 级热力色阶（t∈[0,1] 量化到 8 档，由冷到暖）
// 暗色模式整体压暗（各通道 ×0.72）：同样的色相在深色背景上视觉亮度更低，
// 避免高亮档（黄/橙）在暗色下刺眼。
static UiColor heatColor(float t, bool dark) {
    static const int stops[8][3] = {
        { 63, 120, 244 },  { 56, 190, 242 },  { 84, 214, 196 },  { 118, 226, 116 },
        { 186, 222, 74 },  { 249, 205, 66 },  { 245, 148, 66 },  { 236, 88, 78 }
    };
    if (t <= 0) t = 0; if (t > 1) t = 1;
    int i = (int)(t * 7.999f);
    float k = dark ? 0.72f : 1.0f;
    return rgb255((int)(stops[i][0] * k), (int)(stops[i][1] * k), (int)(stops[i][2] * k));
}

// ---- 热力图归一化 ----
// 之前固定按 cap=可见最大值做 log 变换：离群高值把色阶顶得极高，其余样本全被
// 压进低档、最低档几乎不出现；隐藏键后 cap 若没变（隐藏的不是最大值），热度就
// 不重排。改为在“当前可见非零样本的最小→最大”之间做 log 插值，使可见键/格总是
// 铺满 8 档色阶：隐藏任意键后 min/max 重算，热度立即重新排序、低档颜色随之出现；
// 键盘侧样本已按当前配列过滤，各配列独立归一化，不再受整键盘数据影响。
static void heatVisibleRange(const std::vector<uint32_t>& vals, uint32_t& mn, uint32_t& mx) {
    if (vals.empty()) { mn = 1; mx = 1; return; }
    mn = vals[0]; mx = vals[0];
    for (size_t i = 1; i < vals.size(); ++i) {
        if (vals[i] > mx) mx = vals[i];
        if (vals[i] < mn) mn = vals[i];
    }
    if (mx == 0) mx = 1;
    if (mn == 0) mn = 1;
}
// c 在 [mn,mx] 之间做 log 插值：越靠 mn 越冷（低档），越靠 mx 越热（高档）。
static float heatT(uint32_t c, uint32_t mn, uint32_t mx) {
    if (c == 0) return 0.0f;
    if (mx <= mn) return 1.0f;   // 可见样本全相等：取最高档
    double lc = std::log1p((double)(c > mx ? mx : c));
    double lmin = std::log1p((double)mn);
    double lmax = std::log1p((double)mx);
    double t = (lc - lmin) / (lmax - lmin);
    if (t < 0) t = 0; if (t > 1) t = 1;
    return (float)t;
}
static std::wstring widen(const std::string& s) {
    // 正确解码 UTF-8 → UTF-16（ASCII 原样通过，中文如 "月"/"一" 正常显示）
    std::wstring w; w.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        uint32_t cp = 0; int len = 0;
        if ((c & 0x80) == 0)          { cp = c;        len = 1; }
        else if ((c & 0xE0) == 0xC0)  { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07; len = 4; }
        else                          { w.push_back(c); i++; continue; }
        if (i + len > s.size()) break;
        for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += len;
        if (cp < 0x10000) w.push_back((wchar_t)cp);
        else { cp -= 0x10000; w.push_back((wchar_t)(0xD800 | (cp >> 10))); w.push_back((wchar_t)(0xDC00 | (cp & 0x3FF))); }
    }
    return w;
}

// 键盘热力图：点击按键隐藏/恢复（隐藏后仍计数，仅不显示热度），悬停显示次数。
// 颜色归一化排除已隐藏按键 —— 隐藏/恢复后其余按键热度立即重新分配色阶。
// 绘制时把布局参数写入全局缓存，供鼠标回调换算坐标。
static void KeyHeatDraw(UiWidget, UiDrawCtx ctx, UiRect rect, void*) {
    const auto& kc = cumulativeKeys();  
    bool dark = (ui_theme_get_mode() == UI_THEME_DARK);

    float pad = 12.0f;
    float availW = (rect.right - rect.left) - pad * 2;
    float availH = (rect.bottom - rect.top) - pad * 2;
    if (availW < 10 || availH < 10) { g_khUnit = 0; return; }

    // 键与键之间的视觉间隔（px）；分区之间 = 2×gap（见 zoneOffset）。
    // 键间距与圆角半径随缩放(unit)同比缩放，保持观感一致：先以基准 gap 求 unit，
    // 再按 unit 比例缩放 gap/radius，最后用缩放后的 gap 重算一次 unit 收敛。
    int   zc = layoutZoneCount();
    float zsum = layoutZoneUnitsSum();
    float maxY = layoutBoardMaxY();
    bool widenTop = (app().kbLayout != 2);   // 108/87：顶部行与下方行间距 = 2×gap（同分区横向间距）
    float gap = g_khGap;                       // 基准间距（首次 ≈5px）
    float interGap = 2.0f * gap * (zc - 1);
    float sW = (availW - interGap) / zsum;
    float extraTop = widenTop ? gap : 0.0f;    // 顶部行拉开 reserved 的额外高度
    float sH = (availH + gap - extraTop) / maxY;
    float s = sW < sH ? sW : sH;
    gap    = std::max(2.0f, s * 0.07f);        // 键间距 ∝ 缩放
    float radius = std::max(2.0f, s * 0.09f);  // 圆角半径 ∝ 缩放
    interGap = 2.0f * gap * (zc - 1);
    sW = (availW - interGap) / zsum;
    extraTop = widenTop ? gap : 0.0f;
    sH = (availH + gap - extraTop) / maxY;
    s = sW < sH ? sW : sH;
    g_khGap = gap;                             // 命中测试复用同一缩放间距
    float unitW = s, unitH = s;
    float totalW = zsum * s + interGap;
    float totalH = maxY * unitH - gap;
    float ox = rect.left + pad + (availW - totalW) * 0.5f;
    float oy = rect.top + pad + (availH - totalH) * 0.5f;
    g_khOx = ox; g_khOy = oy; g_khUnit = s;

    float fLabel = unitW * 0.30f; if (fLabel < 7.0f) fLabel = 7.0f;
    float fCount = unitW * 0.22f; if (fCount < 6.0f) fCount = 6.0f;

       std::vector<uint32_t> samples;
    samples.reserve(kc.size());
    for (auto& kv : kc) {
        if (app().hiddenKeys.count(kv.first)) continue;
        if (kv.second > 0 && vkInLayout(kv.first)) samples.push_back(kv.second);
    }
    // 可见样本无活跃时 min=max=1，所有有值键取最高档
    uint32_t hmn = 1, hmx = 1;
    heatVisibleRange(samples, hmn, hmx);

    for (int i = 0; i < kNumKeys; ++i) {
        const KeyCell& kd = kKeys[i];
        if (!keyInLayout(kd)) continue;

        float kxGrid = kd.x, kyGrid = kd.y;
        if (app().kbLayout == 2) {
            // 61键：去掉F行，坐标整体上移一行(0..4)。Esc 占据原 ` 槽位(x=0, 数字行)，
            // 与数字行 `1234…` 对齐。
            if (kd.vk == 27) { kxGrid = 0; kyGrid = 0; }
            else kyGrid = kd.y - 1.0f;
        }
        float kx = ox + zoneOffset(kd.zone, unitW, gap) + kxGrid * unitW;
        float ky = oy + kyGrid * unitH;
        if (widenTop && kyGrid >= 1.0f) ky += gap;   // 顶部行(F/控制顶三键)与下方行拉开到 2×gap
        float kw = kd.w * unitW - gap;
        float kh = kd.h * unitH - gap;
        if (kw <= 0 || kh <= 0) continue;
        UiRect kr = { kx, ky, kx + kw, ky + kh };
        bool hidden = app().hiddenKeys.count(kd.vk) > 0;
        uint32_t c = 0;
        auto it = kc.find(kd.vk);
        if (it != kc.end()) c = it->second;

        if (hidden) {
            // 隐藏键：弱化的空心样式，计数照常但热度不显示
            ui_draw_fill_rounded_rect(ctx, kr, radius, radius,
                dark ? rgb255(42, 46, 54) : rgb255(243, 245, 248));
            ui_draw_rounded_rect(ctx, kr, radius, radius,
                dark ? rgb255(80, 86, 96) : rgb255(214, 219, 226), 1.2f);
            UiColor tc = dark ? rgb255(110, 116, 126) : rgb255(170, 176, 186);
            if (kd.l[0] != 0) {
                UiRect lr = { kr.left, kr.top, kr.right, kr.top + kh * 0.56f };
                ui_draw_text_ex(ctx, kd.l, lr, tc, fLabel, 2, 0);
            }
            continue;
        }

        UiColor bg = c > 0 ? heatColor(heatT(c, hmn, hmx), dark)
                           : (dark ? rgb255(46, 50, 58) : rgb255(231, 234, 239));
        float r = radius; if (r > kw * 0.5f) r = kw * 0.5f; if (r > kh * 0.5f) r = kh * 0.5f;
        ui_draw_fill_rounded_rect(ctx, kr, r, r, bg);
        UiColor tc = c > 0 ? rgb255(255, 255, 255) : (dark ? rgb255(148, 154, 164) : rgb255(95, 102, 114));
        if (kd.l[0] != 0) {
            UiRect lr = { kr.left, kr.top, kr.right, kr.top + kh * 0.56f };
            ui_draw_text_ex(ctx, kd.l, lr, tc, fLabel, 2, 0);
        }
        if (c > 0) {
            std::wstring cs = std::to_wstring(c);
            UiRect cr = { kr.left, kr.top + kh * 0.42f, kr.right, kr.bottom };
            ui_draw_text_ex(ctx, cs.c_str(), cr, tc, fCount, 2, 0);
        }
    }
}

// 命中测试：widget-local 坐标 -> 键（不命中返回 nullptr），布局换算与 KeyHeatDraw 一致
static const KeyCell* keyHitTest(float x, float y) {
    if (g_khUnit <= 0) return nullptr;
    for (int i = 0; i < kNumKeys; ++i) {
        const KeyCell& kd = kKeys[i];
        if (!keyInLayout(kd)) continue;
        float kxGrid = kd.x, kyGrid = kd.y;
        if (app().kbLayout == 2) {
            if (kd.vk == 27) { kxGrid = 0; kyGrid = 0; }
            else kyGrid = kd.y - 1.0f;
        }
        float kx = g_khOx + zoneOffset(kd.zone, g_khUnit, g_khGap) + kxGrid * g_khUnit;
        float ky = g_khOy + kyGrid * g_khUnit;
        if (app().kbLayout != 2 && kyGrid >= 1.0f) ky += g_khGap;   // 与 KeyHeatDraw 同款顶部行间距
        float kw = kd.w * g_khUnit - g_khGap;
        float kh = kd.h * g_khUnit - g_khGap;
        if (kw <= 0 || kh <= 0) continue;
        if (x >= kx && x <= kx + kw && y >= ky && y <= ky + kh) return &kd;
    }
    return nullptr;
}

// 反转单个 vk 的显示状态（隐藏↔恢复），隐藏后仍计数、仅不显示热度
static void toggleKeyHidden(uint8_t vk) {
    auto it = app().hiddenKeys.find(vk);
    if (it != app().hiddenKeys.end()) app().hiddenKeys.erase(it);  // 已隐藏 → 恢复
    else app().hiddenKeys.insert(vk);                              // 未隐藏 → 隐藏
    app().dirty = true;
    app().needsRefresh = true;
    if (g_win) ui_window_invalidate(g_win);
}

static int onKeyHeatMove(UiWidget w, float x, float y, int btn, void*) {
    const KeyCell* kd = keyHitTest(x, y);
    // 左键按住拖动：反转经过的多个按键（每个键在本次路径内只切换一次）
    if (btn == 1) {
        if (kd && kd != g_khDragLast) {
            toggleKeyHidden(kd->vk);
            g_khDragLast = kd;
        }
        return 0;   // 拖拽期间不显示 tooltip
    }
    if (!kd) { ui_widget_set_tooltip(w, nullptr); return 0; }
    uint32_t c = 0;
    auto it = cumulativeKeys().find(kd->vk);
    if (it != cumulativeKeys().end()) c = it->second;
    wchar_t buf[96];
    bool hidden = app().hiddenKeys.count(kd->vk) > 0;
    _snwprintf_s(buf, _TRUNCATE, L"%s%s：累计 %u 次%s",
                 kd->l, kd->l[0] ? L"" : L"Space", c, hidden ? L"（已隐藏热度）" : L"");
    ui_widget_set_tooltip(w, buf);
    return 0;
}

static int onKeyHeatDown(UiWidget w, float x, float y, int btn, void*) {
    g_khDragLast = nullptr;
    if (btn != 1) return 0;
    const KeyCell* kd = keyHitTest(x, y);
    if (!kd) return 0;
    toggleKeyHidden(kd->vk);
    g_khDragLast = kd;   // 按下即算切换首键，后续 move 从下一个不同键继续
    return 1;
}

static int onKeyHeatUp(UiWidget w, float x, float y, int btn, void*) {
    g_khDragLast = nullptr;   // 抬起左键，拖拽反转结束
    return 0;
}

static void onKeyHeatLeave(UiWidget w, void*) {
    g_khDragLast = nullptr;   // 光标离开画布时停止拖拽
    ui_widget_set_tooltip(w, nullptr);
}

// 鼠标热力图：点击位置累计热力，悬停显示次数，无图例。
static void MouseHeatDraw(UiWidget, UiDrawCtx ctx, UiRect rect, void*) {
    const auto& heat = cumulativeHeat();
    // p98 截断 + 对数归一化：离群热点封顶，其余位置层次拉开
    std::vector<uint32_t> samples;
    samples.reserve(heat.size());
    for (auto& kv : heat) if (kv.second > 0) samples.push_back(kv.second);
    // 在可见非零样本 min→max 之间对数插值，热点铺满 8 档色阶
    uint32_t hmn = 1, hmx = 1;
    heatVisibleRange(samples, hmn, hmx);
    bool dark = (ui_theme_get_mode() == UI_THEME_DARK);

    float pad = 12.0f;
    float availW = (rect.right - rect.left) - pad * 2;
    float availH = (rect.bottom - rect.top) - pad * 2;
    if (availW <= 0 || availH <= 0) { g_mhValid = false; return; }

    // 按 16:9 网格等比缩放并居中，避免网格被拉伸变形
    float aspect = (float)kHeatW / (float)kHeatH; // 48/27 ≈ 16:9
    float gw = availW, gh = availH;
    if (gw / gh > aspect) gw = gh * aspect;
    else gh = gw / aspect;
    float ox = rect.left + pad + (availW - gw) * 0.5f;
    float oy = rect.top + pad + (availH - gh) * 0.5f;

    float cw = gw / kHeatW, ch = gh / kHeatH;
    float gap = 1.0f; // 格间留缝，露出底色的弱化网格线
    g_mhOx = ox; g_mhOy = oy; g_mhCw = cw; g_mhCh = ch; g_mhValid = true;

    ui_draw_fill_rect(ctx, UiRect{ ox, oy, ox + gw, oy + gh },
                      dark ? rgb255(34, 38, 45) : rgb255(222, 227, 234));

    float r = 2.0f; if (r > cw * 0.5f) r = cw * 0.5f; if (r > ch * 0.5f) r = ch * 0.5f;
    UiColor cellBg0 = dark ? rgb255(40, 44, 52) : rgb255(243, 246, 249);
    for (int y = 0; y < kHeatH; ++y) {
        for (int x = 0; x < kHeatW; ++x) {
            uint32_t idx = (uint32_t)(y * kHeatW + x);
            uint32_t c = 0;
            auto it = heat.find(idx);
            if (it != heat.end()) c = it->second;
            UiRect cr = { ox + x * cw + gap, oy + y * ch + gap, ox + (x + 1) * cw - gap, oy + (y + 1) * ch - gap };
            UiColor col = c > 0 ? heatColor(heatT(c, hmn, hmx), dark) : cellBg0;
            ui_draw_fill_rounded_rect(ctx, cr, r, r, col);
        }
    }

    // 悬浮计数覆层：直接在悬停格旁绘制深色圆角框 + 累计次数文字。
    // （不用 tooltip —— 那套在本场景渲染成底部黑框且被点击吞掉，直绘最可靠）
    // 重置前布料完整校验 gx/gy 均在网格范围内，避免越界（尤其顶部越界）残留旧浮窗
    if (g_mhHoverGx >= 0 && g_mhHoverGx < kHeatW &&
        g_mhHoverGy >= 0 && g_mhHoverGy < kHeatH) {
        uint32_t c = 0;
        uint32_t idx = (uint32_t)(g_mhHoverGy * kHeatW + g_mhHoverGx);
        auto it = heat.find(idx);
        if (it != heat.end()) c = it->second;
        if (c > 0) {
            wchar_t buf[64];
            _snwprintf_s(buf, _TRUNCATE, L"%u 次", c);
            // 悬停框按文本宽度自适应；默认放光标左上方（避免被右手挡住），
            // 贴边时回退到另一侧，始终保证框不出画布。
            float tw = ui_draw_measure_text(ctx, buf, 12);
            float bw = tw + 16;
            float bh = 22.0f;
            float by = g_mhHoverY - bh - 8;         // 默认在光标上方
            if (by < rect.top) by = g_mhHoverY + 12; // 上方放不下 → 下方
            float bx = g_mhHoverX - bw - 10;        // 默认在光标左侧
            if (bx < rect.left) bx = g_mhHoverX + 10; // 左侧放不下 → 右侧
            UiRect br = { bx, by, bx + bw, by + bh };
            ui_draw_fill_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(24, 26, 31, 235));
            ui_draw_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(120,126,136,120), 1.0f);
            ui_draw_text_ex(ctx, buf, UiRect{ br.left + 8, br.top, br.right - 4, br.bottom },
                            rgb255(255, 255, 255), 12, 2, 0);
        }
    }
}

// 悬停显示该格累计点击次数（直绘覆层；越界/离开时清除）
// 注意：务必用“裸坐标”判定是否落在绘图网格内。仅看格坐标 (gx,gy) 会因为 C++
// (int) 向零截断，在指针只从网格顶边/左边越出一点点时 (y-ox 仍是 (0, -ch) 内) 算出
// gy==0 仍落在 [0,kH) 范围内，导致浮窗不清除/残留。
static int onMouseHeatMove(UiWidget w, float x, float y, int, void*) {
    bool inside = g_mhValid && g_mhCw > 0 && g_mhCh > 0 &&
                  x >= g_mhOx && x < g_mhOx + kHeatW * g_mhCw &&
                  y >= g_mhOy && y < g_mhOy + kHeatH * g_mhCh;
    if (!inside) {
        if (g_mhHoverGx >= 0) {
            g_mhHoverGx = -1; g_mhHoverGy = -1;
            if (g_win) ui_window_invalidate(g_win);
        }
        return 0;
    }
    int gx = (int)((x - g_mhOx) / g_mhCw);
    int gy = (int)((y - g_mhOy) / g_mhCh);
    if (gx < 0 || gx >= kHeatW || gy < 0 || gy >= kHeatH) {
        if (g_mhHoverGx >= 0) { g_mhHoverGx = -1; g_mhHoverGy = -1; if (g_win) ui_window_invalidate(g_win); }
        return 0;
    }
    g_mhHoverGx = gx; g_mhHoverGy = gy; g_mhHoverX = x; g_mhHoverY = y;
    if (g_win) ui_window_invalidate(g_win);
    return 0;
}

struct TrendPoint { std::string label; uint64_t k, c; };

// 从 dayIndexToStr 结果解析 y/m/d（"YYYY-MM-DD"）
static void ymdFromDayIndex(int idx, int& y, int& m, int& d) {
    std::string ds = dayIndexToStr(idx);
    y = m = d = 0;
    if (ds.size() >= 10) {
        y = atoi(ds.substr(0, 4).c_str());
        m = atoi(ds.substr(5, 2).c_str());
        d = atoi(ds.substr(8, 2).c_str());
    }
}

// 解析趋势图选定的年/月/日（由 .uix 日期输入提供；未设置时默认当天）
static void resolveTrendRange(int& y, int& m, int& d) {
    if (g_trendSelY < 2020) {          // 尚未收到任何有效日期输入
        ymdFromDayIndex(app().cur, y, m, d);
    } else {
        y = g_trendSelY; m = g_trendSelM; d = g_trendSelD;
    }
    if (g_trendMode == 1) d = 1;           // 按月只取 y/m
    if (g_trendMode == 2) { m = 1; d = 1; } // 按年只取 y
}

static void TrendDraw(UiWidget, UiDrawCtx ctx, UiRect rect, void*) {
    auto& days = app().days;
    bool dark = (ui_theme_get_mode() == UI_THEME_DARK);

    int y = 0, m = 0, d = 0;
    resolveTrendRange(y, m, d);

    std::vector<TrendPoint> pts;
    char lbuf[32];
    if (g_trendMode == 0) { // 按日：0-24 时
        int base = dayIndexFromYMD(y, m, d);
        auto it = days.find((uint16_t)base);
        for (int h = 0; h < 24; ++h) {
            TrendPoint p; p.k = 0; p.c = 0;
            if (it != days.end()) { p.k = it->second.hourlyKeys[h]; p.c = it->second.hourlyClicks[h]; }
            snprintf(lbuf, sizeof(lbuf), "%02d", h);
            p.label = lbuf;
            pts.push_back(p);
        }
    } else if (g_trendMode == 1) { // 按月：该月全部日期
        int dim = daysInMonth(y, m);
        for (int i = 1; i <= dim; ++i) {
            TrendPoint p; p.k = 0; p.c = 0;
            auto it = days.find((uint16_t)dayIndexFromYMD(y, m, i));
            if (it != days.end()) { p.k = it->second.keys; p.c = it->second.clicks; }
            snprintf(lbuf, sizeof(lbuf), "%d", i);
            p.label = lbuf;
            pts.push_back(p);
        }
    } else if (g_trendMode == 2) { // 按年：12 个月
        for (int i = 1; i <= 12; ++i) {
            TrendPoint p; p.k = 0; p.c = 0;
            int mStart = dayIndexFromYMD(y, i, 1);
            int mEnd = mStart + daysInMonth(y, i) - 1;
            for (auto kv = days.lower_bound((uint16_t)mStart); kv != days.end() && (int)kv->first <= mEnd; ++kv) {
                p.k += kv->second.keys; p.c += kv->second.clicks;
            }
            snprintf(lbuf, sizeof(lbuf), "%d月", i);
            p.label = lbuf;
            pts.push_back(p);
        }
    } else { // 按周：周一 → 周日 共 7 天（起始日为 weekStr 选中的周一）
        int base = dayIndexFromYMD(y, m, d);
        // 显式 UTF-8 字节，避免依赖编译期执行字符集（widen 会正确解码）
        static const char* wd[] = { "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89",
                                    "\xE5\x9B\x9B", "\xE4\xBA\x94", "\xE5\x85\xAD",
                                    "\xE6\x97\xA5" };   // 一二三四五六日
        for (int i = 0; i < 7; ++i) {
            TrendPoint p; p.k = 0; p.c = 0;
            auto it = days.find((uint16_t)(base + i));
            if (it != days.end()) { p.k = it->second.keys; p.c = it->second.clicks; }
            snprintf(lbuf, sizeof(lbuf), "%s", wd[i]);
            p.label = lbuf;
            pts.push_back(p);
        }
    }

    size_t n = pts.size();
    UiColor axisCol = dark ? rgb255(120, 126, 136) : rgb255(138, 145, 157);
    if (n == 0) {
        ui_draw_text_ex(ctx, L"暂无数据", UiRect{ rect.left, rect.top, rect.right, rect.top + 24 }, axisCol, 12, 1, 0);
        return;
    }

    uint64_t maxK = 1, maxC = 1;
    for (auto& p : pts) { if (p.k > maxK) maxK = p.k; if (p.c > maxC) maxC = p.c; }
    // 按键与点击共用同一量程：纵轴标签取两者最大值，柱高按公共比例缩放，
    // 避免两序列各自归一化导致“数值不同但柱高相同”、纵轴只显示按键最大值
    uint64_t maxV = maxK > maxC ? maxK : maxC;

    float pl = 48, pr = 12, pt = 12, pb = 26;
    float cw = (rect.right - rect.left) - pl - pr;
    float ch = (rect.bottom - rect.top) - pt - pb;
    if (cw <= 0 || ch <= 0) return;
    float baseX = rect.left + pl, baseY = rect.top + pt + ch;

    std::wstring ylab = std::to_wstring(maxV);
    ui_draw_text_ex(ctx, ylab.c_str(), UiRect{ rect.left, rect.top + pt - 6, rect.left + pl - 4, rect.top + pt + 6 }, axisCol, 9, 1, 0);
    UiColor gridCol = dark ? rgb255(52, 57, 66) : rgb255(238, 241, 245);
    for (int g = 0; g <= 3; ++g) {
        float gy = baseY - ch * g / 3.0f;
        ui_draw_line(ctx, baseX, gy, baseX + cw, gy, gridCol, 1.0f);
    }

    float slot = cw / (float)n;
    float barW = slot * 0.36f;
    if (barW < 1.0f) barW = 1.0f;
    // 缓存命中几何（供 onTrendMove 换算柱下标）
    g_trBaseX = baseX; g_trSlot = slot; g_trN = n;
    g_trTopY = rect.top + pt; g_trBotY = baseY;
    // 横轴标签按可用宽度自适应抽稀，避免重叠显示不全
    float labelW = 0.0f;
    {
        std::wstring wl = widen(pts[0].label);
        labelW = ui_draw_measure_text(ctx, wl.c_str(), 9) + 6.0f;
    }
    int step = (int)(labelW / slot) + 1; if (step < 1) step = 1;

    for (size_t i = 0; i < n; ++i) {
        TrendPoint& p = pts[i];
        float cx = baseX + slot * i + slot * 0.5f;
        float hk = ch * ((float)p.k / (float)maxV);
        float hc = ch * ((float)p.c / (float)maxV);
        ui_draw_fill_rect(ctx, UiRect{ cx - barW - 1, baseY - hk, cx - 1, baseY }, rgb255(47, 110, 242));
        ui_draw_fill_rect(ctx, UiRect{ cx + 1, baseY - hc, cx + barW + 1, baseY }, rgb255(58, 199, 242));
        if ((int)(i % (size_t)step) == 0) {
            std::wstring wl = widen(p.label);
            UiRect lr = { baseX + slot * i, baseY + 4, baseX + slot * (i + 1), baseY + 20 };
            ui_draw_text_ex(ctx, wl.c_str(), lr, axisCol, 9, 2, 0);
        }
    }

    // 悬停浮窗：命中柱时直绘数值框（与鼠标热力图一致；默认放光标左上方）
    if (g_trHover >= 0 && (size_t)g_trHover < n) {
        const TrendPoint& p = pts[(size_t)g_trHover];
        std::wstring wl = widen(p.label);
        if (g_trendMode == 3) wl = L"周" + wl;   // 周标签仅单字，加前缀消歧
        wchar_t tb[96];
        _snwprintf_s(tb, _TRUNCATE, L"%s：按键 %llu · 点击 %llu", wl.c_str(),
                     (unsigned long long)p.k, (unsigned long long)p.c);
        float tw = ui_draw_measure_text(ctx, tb, 12);
        float bw = tw + 16, bh = 22;
        float by = g_trHoverY - bh - 8;            if (by < rect.top) by = g_trHoverY + 12;
        float bx = g_trHoverX - bw - 10;           if (bx < rect.left) bx = g_trHoverX + 10;
        UiRect br = { bx, by, bx + bw, by + bh };
        ui_draw_fill_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(24, 26, 31, 235));
        ui_draw_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(120, 126, 136, 120), 1.0f);
        ui_draw_text_ex(ctx, tb, UiRect{ br.left + 8, br.top, br.right - 4, br.bottom },
                        rgb255(255, 255, 255), 12, 2, 0);
    }
}

// 趋势图悬停：换算鼠标 x → 柱下标，更新悬浮浮窗；越界/离开时清除
static int onTrendMove(UiWidget, float x, float y, int, void*) {
    int idx = -1;
    // 命中要求：x 落在柱区范围，且 y 落在绘图区上下边界内（否则向上/向下移出图表也不收起）
    if (g_trN > 0 && g_trSlot > 0 && x >= g_trBaseX && y >= g_trTopY && y <= g_trBotY) {
        int i = (int)((x - g_trBaseX) / g_trSlot);
        if (i >= 0 && (size_t)i < g_trN) idx = i;
    }
    if (idx != g_trHover) {
        g_trHover = idx;
        if (g_win) ui_window_invalidate(g_win);
    }
    g_trHoverX = x; g_trHoverY = y;
    return 0;
}

static void onMouseHeatLeave(UiWidget, void*);
static void onTrendLeave(UiWidget, void*);
static void onKeyHeatMount(UiPage, UiWidget w, void*) {
    // 预置非空 tooltip，让引擎在指针首次进入时就开始计时显示（否则首次进入时
    // tooltip 为空，计时器不启动，悬停提示要再次进出 widget 才会出现）
    ui_widget_set_tooltip(w, L" ");
    ui_custom_on_draw(w, KeyHeatDraw, nullptr);
    ui_custom_on_mouse_down(w, onKeyHeatDown, nullptr);
    ui_custom_on_mouse_move(w, onKeyHeatMove, nullptr);
    ui_custom_on_mouse_up(w, onKeyHeatUp, nullptr);
    ui_widget_on_mouse_leave(w, onKeyHeatLeave, nullptr);
}
static void onKeyHeatUnmount(UiPage, UiWidget, void*) { g_khUnit = 0; }
static void onMouseHeatMount(UiPage, UiWidget w, void*) {
    ui_custom_on_draw(w, MouseHeatDraw, nullptr);
    ui_custom_on_mouse_move(w, onMouseHeatMove, nullptr);
    ui_widget_on_mouse_leave(w, onMouseHeatLeave, nullptr);
}
static void onMouseHeatUnmount(UiPage, UiWidget, void*) {
    g_mhValid = false;
    g_mhHoverGx = -1; g_mhHoverGy = -1;
}
// 光标移出鼠标热力图画布 → 收起计数浮窗
static void onMouseHeatLeave(UiWidget, void*) {
    if (g_mhHoverGx >= 0 || g_mhHoverGy >= 0) {
        g_mhHoverGx = -1; g_mhHoverGy = -1;
        if (g_win) ui_window_invalidate(g_win);
    }
}
static void onTrendMount(UiPage, UiWidget w, void*) {
    ui_custom_on_draw(w, TrendDraw, nullptr);
    ui_custom_on_mouse_move(w, onTrendMove, nullptr);
    ui_widget_on_mouse_leave(w, onTrendLeave, nullptr);
}
// 光标移出趋势图画布 → 收起数值浮窗
static void onTrendLeave(UiWidget, void*) {
    if (g_trHover >= 0) {
        g_trHover = -1;
        if (g_win) ui_window_invalidate(g_win);
    }
}

// 从 JSON 字符串中提取一段纯数字/字母文本（用于读 .uix 传来的日期字符串）
static std::string jsonText(const char* json, const char* fallback) {
    if (!json) return fallback;
    const char* p = strchr(json, '"');
    if (!p) return fallback;
    ++p;
    std::string out;
    while (*p && *p != '"') { if (*p != '\\') out.push_back(*p); ++p; }
    return out.empty() ? fallback : out;
}
// 宽松日期解析：提取字符串中的数字，支持 2026-08-30 / 2026.8.30 / 20260830 等写法。
// need=8 取年月日，need=6 取年月。解析失败返回 false。
static bool parseYMDLoose(const std::string& s, int need, int& y, int& m, int& d) {
    std::string digits;
    for (char c : s) if (c >= '0' && c <= '9') digits.push_back(c);
    if ((int)digits.size() < need) return false;
    y = atoi(digits.substr(0, 4).c_str());
    m = atoi(digits.substr(4, 2).c_str());
    d = (need >= 8) ? atoi(digits.substr(6, 2).c_str()) : 1;
    return y >= 2020 && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, m);
}

static void doExportImport(bool doExport) {
    wchar_t file[MAX_PATH] = {};
    if (doExport) wcscpy_s(file, L"KeyMouseTracker-backup.kmt");
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"键鼠使用记录备份 (*.kmt)\0*.kmt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"kmt";
    ofn.Flags = doExport ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    BOOL ok = doExport ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return;
    if (doExport) {
        if (!saveData(file))
            MessageBoxW(g_hwnd, L"导出失败：无法写入目标文件。", L"键鼠使用记录", MB_OK | MB_ICONERROR);
    } else {
        if (MessageBoxW(g_hwnd, L"导入将覆盖当前全部数据，确定继续？",
                        L"键鼠使用记录", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
            return;
        if (loadData(file)) {
            ensureCurDay();
            app().dirty = true;
            app().needsRefresh = true;
        } else {
            MessageBoxW(g_hwnd, L"导入失败：文件无效或不是有效的数据备份。",
                        L"键鼠使用记录", MB_OK | MB_ICONERROR);
        }
    }
}

static void pollCommands() {
    if (!g_page) return;
    int pc = 0, ac = 0, ec = 0, tc = 0, xc = 0, ic = 0, clc = 0;
    if (char* j = ui_page_get_json(g_page, "pauseCmd")) { pc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "autostartCmd")) { ac = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exitCmd")) { ec = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "themeCmd")) { tc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exportCmd")) { xc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "importCmd")) { ic = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "clearCmd")) { clc = jsonInt(j, 0); ui_page_free(j); }

    if (pc != g_lastPauseCmd) {
        g_lastPauseCmd = pc;
        app().paused = !app().paused;
        app().dirty = true;
        app().needsRefresh = true;
        UpdateTray(app().paused);
        refreshTrayCheck();
    }
    if (ac != g_lastAutoCmd) {
        g_lastAutoCmd = ac;
        SetAutoStart(!IsAutoStart());
        app().needsRefresh = true;
        refreshTrayCheck();
    }
    if (ec != g_lastExitCmd) {
        g_lastExitCmd = ec;
        ui_quit(0);
    }
    if (tc != g_lastThemeCmd) {
        g_lastThemeCmd = tc;
        app().darkTheme = !app().darkTheme;
        app().dirty = true;
        ui_theme_set_mode(app().darkTheme ? UI_THEME_DARK : UI_THEME_LIGHT);
        if (g_page) ui_page_set_bool(g_page, "dark", app().darkTheme ? 1 : 0);
        app().needsRefresh = true;
    }
    if (xc != g_lastExportCmd) {
        g_lastExportCmd = xc;
        doExportImport(true);
    }
    if (ic != g_lastImportCmd) {
        g_lastImportCmd = ic;
        doExportImport(false);
    }
    if (clc != g_lastClearCmd) {
        g_lastClearCmd = clc;
        if (MessageBoxW(g_hwnd, L"确定清除全部统计记录？此操作不可恢复，建议先导出备份。",
                        L"键鼠使用记录 - 清除数据", MB_OKCANCEL | MB_ICONWARNING) == IDOK) {
            clearAllData();
            ui_page_set_json(g_page, "stats", buildStatsJson().c_str());
            if (g_win) ui_window_invalidate(g_win);
        }
    }

    // 打开外部链接（关于页：项目地址 / 参考引用）
    if (char* lj = ui_page_get_json(g_page, "linkCmd")) {
        int lc = jsonInt(lj, 0);
        if (lc != g_lastLinkCmd) {
            g_lastLinkCmd = lc;
            std::string url;
            if (char* uj = ui_page_get_json(g_page, "linkUrl")) {
                url = jsonText(uj, "");
                ui_page_free(uj);
            }
            if (!url.empty())
                ShellExecuteW(g_hwnd, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ui_page_free(lj);
    }

    // 趋势图模式切换
    if (char* j = ui_page_get_json(g_page, "trendModeIdx")) {
        int m = jsonInt(j, g_trendMode);
        if (m < 0 || m > 3) m = g_trendMode;
        if (m != g_trendMode) { g_trendMode = m; app().needsRefresh = true; }
        ui_page_free(j);
    }
    // 键盘配列切换
    if (char* j = ui_page_get_json(g_page, "kbLayout")) {
        int l = jsonInt(j, (int)app().kbLayout);
        if (l < 0 || l > 2) l = app().kbLayout;
        if (l != (int)app().kbLayout) {
            app().kbLayout = (uint8_t)l;
            app().dirty = true;
            app().needsRefresh = true;  // 触发重绘，热力图按新配列渲染
        }
        ui_page_free(j);
    }
    // 日期选择
    bool dateDirty = false;
    if (g_trendMode == 0) {
        if (char* j = ui_page_get_json(g_page, "dateStr")) {
            std::string s = jsonText(j, "");
            int y, m, d;
            if (parseYMDLoose(s, 8, y, m, d) &&
                (y != g_trendSelY || m != g_trendSelM || d != g_trendSelD)) {
                g_trendSelY = y; g_trendSelM = m; g_trendSelD = d;
                dateDirty = true;
            }
            ui_page_free(j);
        }
    } else if (g_trendMode == 3) { // 按周：weekStr 为周一日期
        if (char* j = ui_page_get_json(g_page, "weekStr")) {
            std::string s = jsonText(j, "");
            int y, m, d;
            if (parseYMDLoose(s, 8, y, m, d) &&
                (y != g_trendSelY || m != g_trendSelM || d != g_trendSelD)) {
                g_trendSelY = y; g_trendSelM = m; g_trendSelD = d;
                dateDirty = true;
            }
            ui_page_free(j);
        }
    } else if (g_trendMode == 1) {
        if (char* j = ui_page_get_json(g_page, "monthStr")) {
            std::string s = jsonText(j, "");
            int y, m, d;
            if (parseYMDLoose(s, 6, y, m, d) &&
                (y != g_trendSelY || m != g_trendSelM)) {
                g_trendSelY = y; g_trendSelM = m; g_trendSelD = 1;
                dateDirty = true;
            }
            ui_page_free(j);
        }
    } else {
        if (char* j = ui_page_get_json(g_page, "yearNum")) {
            int y = jsonInt(j, 0);
            if (y >= 2020 && y <= 9999 && y != g_trendSelY) {
                g_trendSelY = y; g_trendSelM = 1; g_trendSelD = 1;
                dateDirty = true;
            }
            ui_page_free(j);
        }
    }
    if (dateDirty) app().needsRefresh = true;
}

static VOID CALLBACK TimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    switch (id) {
    case TI_SAMPLE: {
        POINT pt; GetCursorPos(&pt);
        static POINT s_last = {-1, -1};
        if (pt.x != s_last.x || pt.y != s_last.y) {
            recordMove();
            s_last = pt;
        }
        break;
    }
    case TI_SAVE:
        saveData(dataFilePath());
        break;
    case TI_ACTIVE:
        if (GetTickCount() - app().lastActivity < 2000) {
            ensureCurDay();
            DayData& t = app().days[app().cur];
            t.activeSec++;
            app().dirty = true;
        }
        ensureCurDay();
        break;
    case TI_POLL: {
        pollCommands();
        if (app().needsRefresh) {
            app().needsRefresh = false;
            pushStats();
        }
        break;
    }
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // 单实例互斥
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\KeyMouseTracker.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(nullptr, L"键鼠使用记录");
        if (w) { ShowWindow(w, SW_SHOW); ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
        CloseHandle(mutex);
        return 0;
    }

    // 加载数据
    loadData(dataFilePath());
    ensureCurDay();

    // 初始化 UI
    ui_init_with_theme(app().darkTheme ? UI_THEME_DARK : UI_THEME_LIGHT);
    g_page = ui_page_load_string(k_app_uix);
    if (!g_page) {
        MessageBoxW(nullptr, L"UI 页面加载失败（ui_page_load_string 返回空）。", L"键鼠使用记录", MB_OK | MB_ICONERROR);
        ui_shutdown();
        ReleaseMutex(mutex);
        return 1;
    }

    g_win = ui_page_open_window(g_page, nullptr);
    if (!g_win) {
        const char* err = ui_page_last_error(g_page);
        std::string errStr = err ? err : "(unknown)";
        std::wstring wErr;
        wErr.assign(errStr.begin(), errStr.end());
        MessageBoxW(nullptr, wErr.c_str(), L"键鼠使用记录 - UI 初始化失败", MB_OK | MB_ICONERROR);
        ui_page_destroy(g_page);
        ui_shutdown();
        ReleaseMutex(mutex);
        return 2;
    }
    g_hwnd = (HWND)ui_window_hwnd(g_win);

    // 注册自定义绘制回调
    ui_page_on_widget_mount(g_page, "keyheat_canvas", onKeyHeatMount, nullptr);
    ui_page_on_widget_unmount(g_page, "keyheat_canvas", onKeyHeatUnmount, nullptr);
    ui_page_on_widget_mount(g_page, "mouseheat_canvas", onMouseHeatMount, nullptr);
    ui_page_on_widget_unmount(g_page, "mouseheat_canvas", onMouseHeatUnmount, nullptr);
    ui_page_on_widget_mount(g_page, "trend_canvas", onTrendMount, nullptr);

    ui_window_on_close_request(g_win, OnCloseRequest, nullptr);
    ui_window_on_resize(g_win, OnWindowResize, nullptr);

    ui_page_set_bool(g_page, "dark", app().darkTheme ? 1 : 0);
    {
        char lb[8];
        _snprintf_s(lb, _TRUNCATE, "%d", (int)app().kbLayout);
        ui_page_set_json(g_page, "kbLayout", lb);
    }

    SetWindowSubclass(g_hwnd, SubclassProc, kSubclassId, 0);

    // 安装全局钩子
    if (!InstallHooks()) {
        MessageBoxW(nullptr, L"无法安装全局钩子，请以普通进程身份运行。", L"键鼠使用记录", MB_OK | MB_ICONWARNING);
        ui_page_destroy(g_page);
        ui_shutdown();
        ReleaseMutex(mutex);
        return 3;
    }

    AddTrayIcon();
    UpdateTray(false);

    SetTimer(g_hwnd, TI_SAMPLE, kSampleMs, TimerProc);
    SetTimer(g_hwnd, TI_SAVE, kSaveMs, TimerProc);
    SetTimer(g_hwnd, TI_ACTIVE, 1000, TimerProc);
    SetTimer(g_hwnd, TI_POLL, 500, TimerProc);

    pushStats();

    if (wcsstr(GetCommandLineW(), L"-min") != nullptr) {
        ui_window_hide(g_win);
    } else {
        ui_window_show(g_win);
    }

    int code = ui_run();

    KillTimer(g_hwnd, TI_SAMPLE);
    KillTimer(g_hwnd, TI_SAVE);
    KillTimer(g_hwnd, TI_ACTIVE);
    KillTimer(g_hwnd, TI_POLL);
    RemoveWindowSubclass(g_hwnd, SubclassProc, kSubclassId);

    UninstallHooks();
    saveData(dataFilePath());
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_trayMenu) DestroyMenu(g_trayMenu);

    ui_page_destroy(g_page);
    ui_shutdown();
    ReleaseMutex(mutex);
    return code;
}
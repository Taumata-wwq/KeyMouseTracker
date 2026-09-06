// 主程序（core-ui 版）：现代界面 + 全局键鼠钩子 + 系统托盘 + 持久化
#include <ui_core.h>
#include "data.h"
#include "hooks.h"
#include "autostart.h"
#include "export.h"
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

// 窗口移动/缩放期间为 true：拖拽中停掉重统计 JSON 与全局重绘，
// 全部重活推迟到 WM_EXITSIZEMOVE 一次性完成，保证拖拽全程不卡顿。
static bool g_inResizeMode = false;

// UI 统计刷新采用局部推送：stats 拆为独立顶层键，pushStats 仅推送内容变化的键
// （快照 diff），避免全量替换触发所有绑定重求值；窗口隐藏时跳过刷新，显示后补刷。
static DWORD g_lastPushTick = 0;
static const DWORD kPushMinMs = 16;   // 60 帧节流：TI_REFRESH 每 16ms 检查一次

// pushStats：把当前统计重建为 JSON 并推送到 .uix；定义在下方，SubclassProc 需提前可见
static void pushStats();

// 定时器
enum { TI_SAMPLE = 1, TI_SAVE = 2, TI_ACTIVE = 3, TI_POLL = 4, TI_REFRESH = 5 };
static const UINT kSampleMs = 33;
static const UINT kSaveMs = 30000;
static const UINT kRefreshMs = 16;   // UI 刷新轮询：60 帧

static int g_lastExclCmd = 0;
static int g_lastRemoveExclCmd = 0;

// 当前前台应用 exe 名（UTF-8），TI_POLL 轮询刷新；供 optAppTrack 归因
static void pollForeApp() {
    if (!app().optAppTrack) { setCurrentForeApp(""); return; }
    HWND fg = GetForegroundWindow();
    if (!fg) { setCurrentForeApp(""); return; }
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) { setCurrentForeApp(""); return; }
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) { setCurrentForeApp(""); return; }
    wchar_t buf[MAX_PATH] = {};
    DWORD n = MAX_PATH;
    if (QueryFullProcessImageNameW(hp, 0, buf, &n) && n > 0) {
        // 取文件名（不含路径）
        wchar_t* slash = wcsrchr(buf, L'\\');
        const wchar_t* base = slash ? slash + 1 : buf;
        // UTF-16 → UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, base, -1, nullptr, 0, nullptr, nullptr);
        std::string nm;
        if (len > 1) {
            nm.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, base, -1, &nm[0], len, nullptr, nullptr);
        }
        setCurrentForeApp(nm);
    } else {
        setCurrentForeApp("");
    }
    CloseHandle(hp);
}

enum { IDM_SHOW = 1000, IDM_PAUSE = 1001, IDM_AUTOSTART = 1002, IDM_EXIT = 1003 };

static int g_lastPauseCmd = 0;
static int g_lastAutoCmd = 0;
static int g_lastExitCmd = 0;
static int g_lastThemeCmd = 0;
static int g_lastExportCmd = 0;
static int g_lastImportCmd = 0;
static int g_lastClearCmd = 0;
static int g_lastLinkCmd = 0;
static int g_lastExportCsvCmd = 0;
static int g_lastExportJsonCmd = 0;
static int g_lastRangePickCmd = 0;
static int g_lastRangeCancelCmd = 0;
static int g_lastOptCmd = 0;

// 范围选择弹窗用途：1=导出CSV 2=导出JSON 3=按范围清除（0=空闲）
static int g_pendingReq = 0;
// 范围弹窗确认后解析出的起止日期索引（导出/清除均使用）
static int g_pendingStart = 0, g_pendingEnd = 65535;

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
static float g_trTopY = 0, g_trBotY = 0;
// APM 曲线悬浮状态
static int g_apmHover = -1;
static float g_apmHoverX = 0, g_apmHoverY = 0;
static float g_apmBaseX = 0, g_apmSlot = 0, g_apmTopY = 0, g_apmBotY = 0;    // 绘图区上下边界（y 越界即收起浮窗）
static size_t g_trN = 0;

// 活跃状态机（TI_ACTIVE 使用）：连续活跃段计时
static DWORD g_sessionStartTick = 0;   // 当前活跃段起点 tick
static uint16_t g_sessionDay = 0xFFFF; // 会话段归属日（跨天后重置）

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
    app().needsRefresh = true;   // 恢复显示时置位，由 TI_POLL 补刷隐藏期间累计的数据
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
        if (msg == WM_ENTERSIZEMOVE) {
            /* 必须经 DefSubclassProc 转发给 core-ui：其 WndProc 依赖该消息
             * 置 isMoving_ 才能进入交互缩放 60Hz 合批分支（WM_SIZE 不再逐条
             * 同步 layout/提交）。直接 return 0 会终止消息链，isMoving_ 恒
             * false，每个 WM_SIZE 仍同步整树重排（~160Hz），卡顿依旧。 */
            LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
            g_inResizeMode = true;   // 进入移动/缩放：停止重统计与重绘，拖拽较轻
            return r;
        }
        if (msg == WM_EXITSIZEMOVE) {
            /* 同样先转发：core-ui 需收尾（停合批定时器 + 最终同步布局 +
             * 提交 final 帧），再刷新统计。 */
            LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
            g_inResizeMode = false;
            if (g_page) {
                app().needsRefresh = false;
                pushStats();          // 结束时一次做对：补刷新数据并重绘
            }
            return r;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static int OnCloseRequest(UiWindow win, void*) {
    ui_window_hide(win);
    return 0;
}

// compact 脏检查：仅当 <640px 布尔值真正翻转时才写回页面。此前每帧无条件
// ui_page_set_bool，会触发 JS proxy set-trap → TriggerWrite → 所有依赖
// this.compact 的 {{:fmt(...)}} 文本绑定同步重求值（13-24ms/次），
// 拖动文本页（键盘统计/鼠标统计/关于）时 WM_SIZE 风暴每 16ms 就全量重算一次。
static bool g_compact = false;
static bool g_compactInit = false;   // 哨兵：保证首次回调总是一次同步（窗口初始已窄时）

static void OnWindowResize(UiWindow, int w, int, void*) {
    bool next = (w < 640) ? true : false;
    if (!g_compactInit || next != g_compact) {
        g_compactInit = true;
        g_compact = next;
        if (g_page) ui_page_set_bool(g_page, "compact", next ? 1 : 0);
    }
}

static int jsonInt(const char* json, int fallback) {
    if (!json) return fallback;
    while (*json && !((*json >= '0' && *json <= '9') || *json == '-')) ++json;
    if (!*json) return fallback;
    return (int)strtod(json, nullptr);
}

// 键盘统计表 JSON：累计按键按次数降序，形如 [{"l":"A","c":123}, ...]
static std::string buildKeyTableJson() {
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
        table += "{\"l\":\"" + jsonEscape(l) + "\",\"c\":" + std::to_string(sorted[i].second) + "}";
    }
    table += "]";
    return table;
}

// 今日活跃应用 Top6 JSON（仅采集开启且有当日数据时非空）：[{"n":..,"c":..,"p":..}, ...]
static std::string buildTopAppsJson() {
    auto& days = app().days;
    auto it = days.find(app().cur);
    if (!app().optAppTrack || it == days.end()) return "[]";
    uint64_t appTot = 0;
    for (auto& ac : it->second.appCounts) appTot += ac.second;
    std::vector<std::pair<std::string, uint64_t>> apps;
    for (auto& ac : it->second.appCounts) apps.push_back(ac);
    std::sort(apps.begin(), apps.end(),
              [](const std::pair<std::string, uint64_t>& a, const std::pair<std::string, uint64_t>& b) {
                  return a.second > b.second;
              });
    if (apps.size() > 6) apps.resize(6);
    std::string out = "[";
    for (size_t i = 0; i < apps.size(); ++i) {
        if (i) out += ",";
        int pct = appTot ? (int)(apps[i].second * 100 / appTot) : 0;
        out += "{\"n\":\"" + jsonEscape(apps[i].first.c_str()) + "\",\"c\":" + std::to_string(apps[i].second) +
               ",\"p\":" + std::to_string(pct) + "}";
    }
    out += "]";
    return out;
}

// 24h 应用使用 JSON：今日 + 昨日 appCounts 合并，排除列表内不计，按次数降序
static std::string buildApps24hJson() {
    auto& days = app().days;
    auto it = days.find(app().cur);
    if (!app().optAppTrack) return "[]";
    std::map<std::string, uint64_t> apps24h;
    for (auto& ac : it->second.appCounts) {
        if (!app().excludeApps.count(ac.first)) apps24h[ac.first] += ac.second;
    }
    auto yit = days.find((uint16_t)(app().cur - 1));
    if (yit != days.end()) {
        for (auto& ac : yit->second.appCounts) {
            if (!app().excludeApps.count(ac.first)) apps24h[ac.first] += ac.second;
        }
    }
    std::vector<std::pair<std::string, uint64_t>> apps24hVec(apps24h.begin(), apps24h.end());
    std::sort(apps24hVec.begin(), apps24hVec.end(),
              [](const std::pair<std::string, uint64_t>& a, const std::pair<std::string, uint64_t>& b) {
                  return a.second > b.second;
              });
    uint64_t app24hTot = 0;
    for (auto& ac : apps24h) app24hTot += ac.second;
    std::string out = "[";
    for (size_t i = 0; i < apps24hVec.size(); ++i) {
        if (i) out += ",";
        int pct = app24hTot ? (int)(apps24hVec[i].second * 100 / app24hTot) : 0;
        out += "{\"n\":\"" + jsonEscape(apps24hVec[i].first.c_str()) + "\",\"c\":" + std::to_string(apps24hVec[i].second) +
               ",\"p\":" + std::to_string(pct) + "}";
    }
    out += "]";
    return out;
}

// 关键约束：core-ui 的 set-trap 无值相等判断，set_json 会让依赖键的绑定全部重求值。
// 故拆为独立顶层键 + 快照 diff，只推送内容变化的键。

// 今日概况：{keys, clicks, motion, distCm, activeSec}
static std::string buildTodayJson() {
    auto it = app().days.find(app().cur);
    if (it == app().days.end())
        return "{\"keys\":0,\"clicks\":0,\"motion\":0,\"distCm\":0,\"activeSec\":0}";
    const DayData& d = it->second;
    return "{\"keys\":" + std::to_string(d.keys) + ",\"clicks\":" + std::to_string(d.clicks) +
           ",\"motion\":" + std::to_string(d.motion) + ",\"distCm\":" + std::to_string(distToCm(d.distPx)) +
           ",\"activeSec\":" + std::to_string(d.activeSec) + "}";
}

// 累计概况：{keys, clicks, days, activeSec, distCm}
static std::string buildTotalJson() {
    uint64_t totKeys = 0, totClicks = 0, totActive = 0, totDistPx = 0;
    for (auto& kv : app().days) {
        const DayData& d = kv.second;
        totKeys += d.keys;
        totClicks += d.clicks;
        totActive += d.activeSec;
        totDistPx += d.distPx;
    }
    return "{\"keys\":" + std::to_string(totKeys) + ",\"clicks\":" + std::to_string(totClicks) +
           ",\"days\":" + std::to_string(app().days.size()) +
           ",\"activeSec\":" + std::to_string(totActive) +
           ",\"distCm\":" + std::to_string(distToCm(totDistPx)) + "}";
}

// 今日鼠标按钮：{left, right, mid, total}
static std::string buildMouseTodayJson() {
    auto it = app().days.find(app().cur);
    if (it == app().days.end())
        return "{\"left\":0,\"right\":0,\"mid\":0,\"total\":0}";
    const DayData& d = it->second;
    return "{\"left\":" + std::to_string(d.mLeft) + ",\"right\":" + std::to_string(d.mRight) +
           ",\"mid\":" + std::to_string(d.mMid) +
           ",\"total\":" + std::to_string((uint64_t)d.mLeft + d.mRight + d.mMid) + "}";
}

// 累计鼠标：{left, right, mid, total, motion}
static std::string buildMouseAllJson() {
    uint64_t aL = 0, aR = 0, aM = 0, totMotion = 0;
    for (auto& kv : app().days) {
        const DayData& d = kv.second;
        aL += d.mLeft; aR += d.mRight; aM += d.mMid; totMotion += d.motion;
    }
    return "{\"left\":" + std::to_string(aL) + ",\"right\":" + std::to_string(aR) +
           ",\"mid\":" + std::to_string(aM) + ",\"total\":" + std::to_string(aL + aR + aM) +
           ",\"motion\":" + std::to_string(totMotion) + "}";
}

// 存储概况：数据文件字节数（缓存）、覆盖天数、最早/最晚日期
static std::string buildStorageJson() {
    StorageInfo si = storageInfo();
    return "{\"bytes\":" + std::to_string(si.bytes) +
           ",\"days\":" + std::to_string(si.days) +
           ",\"first\":\"" + (si.days ? dayIndexToStr(si.first) : "") + "\"" +
           ",\"last\":\"" + (si.days ? dayIndexToStr(si.last) : "") + "\"}";
}

// 前台应用排除列表（v-for 数据源）
static std::string buildExcludeListJson() {
    if (app().excludeApps.empty()) return "[]";
    std::string out = "[";
    bool first = true;
    for (auto& e : app().excludeApps) {
        if (!first) out += ",";
        out += "\"" + jsonEscape(e.c_str()) + "\"";
        first = false;
    }
    return out + "]";
}

// 快照：键名 → 上次推送的 JSON 串。内容相同则跳过 set_json，避免无谓绑定重求值
static std::map<std::string, std::string> g_pushSnap;

// 推送单个键（内容变化才写）；返回是否真的写入了
static bool pushKeyIfChanged(const char* name, const std::string& json) {
    auto it = g_pushSnap.find(name);
    if (it != g_pushSnap.end() && it->second == json) return false;
    g_pushSnap[name] = json;
    ui_page_set_json(g_page, name, json.c_str());
    return true;
}

static void pushStats() {
    if (!g_page) return;
    g_lastPushTick = GetTickCount();   // 所有调用路径共享同一节流窗口
    ensureCurDay();
    bool changed = false;
    changed |= pushKeyIfChanged("pausedS",     app().paused ? "true" : "false");
    changed |= pushKeyIfChanged("autostartS",  IsAutoStart() ? "true" : "false");
    changed |= pushKeyIfChanged("optAppTrackS",app().optAppTrack ? "true" : "false");
    changed |= pushKeyIfChanged("todayS",      buildTodayJson());
    changed |= pushKeyIfChanged("totalS",      buildTotalJson());
    changed |= pushKeyIfChanged("mouseTodayS", buildMouseTodayJson());
    changed |= pushKeyIfChanged("mouseAllS",   buildMouseAllJson());
    changed |= pushKeyIfChanged("keyTableS",   buildKeyTableJson());
    changed |= pushKeyIfChanged("appsS",       buildTopAppsJson());
    changed |= pushKeyIfChanged("apps24hS",    buildApps24hJson());
    changed |= pushKeyIfChanged("storageS",    buildStorageJson());
    changed |= pushKeyIfChanged("excludeListS",buildExcludeListJson());
    // 数据确有变化才请求重绘（热力图/趋势图 canvas 直接读 C++ 内存绘制）
    if (changed && g_win) ui_window_invalidate(g_win);
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

    // 中文星期（周模式 / 时段热力行标签共用）：显式 UTF-8 字节，widen 正确解码
    static const char* wd[] = { "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89",
                                "\xE5\x9B\x9B", "\xE4\xBA\x94", "\xE5\x85\xAD",
                                "\xE6\x97\xA5" };   // 一二三四五六日

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
    } else if (g_trendMode == 4) { // 时段热力：周一→周日 × 24h（168 格，键+点击合计显示，悬浮区分）
        int base = dayIndexFromYMD(y, m, d);   // d = 本周周一
        for (int i = 0; i < 168; ++i) {
            TrendPoint p; p.k = 0; p.c = 0;
            auto it = days.find((uint16_t)(base + i / 24));
            int h = i % 24;
            if (it != days.end()) {
                p.k = it->second.hourlyKeys[h];
                p.c = it->second.hourlyClicks[h];
            }
            snprintf(lbuf, sizeof(lbuf), "%s %02d时", wd[i / 24], h);
            p.label = lbuf;
            pts.push_back(p);
        }
    } else { // 按周：周一 → 周日 共 7 天（起始日为 weekStr 选中的周一）
        int base = dayIndexFromYMD(y, m, d);
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

    float pl = 32, pr = 12, pt = 12, pb = 26;
    float cw = (rect.right - rect.left) - pl - pr;
    float ch = (rect.bottom - rect.top) - pt - pb;
    if (cw <= 0 || ch <= 0) return;
    float baseX = rect.left + pl, baseY = rect.top + pt + ch;

    if (g_trendMode == 4) {
        // ---- 时段热力：7（周一~周日）× 24h 方格热力网格 ----
        // 固定方格：cellW == cellH，按画布可用区域等比缩放并居中（与鼠标热力图一致）
        std::vector<uint32_t> hv;
        for (auto& p : pts) { uint32_t s = (uint32_t)(p.k + p.c); if (s > 0) hv.push_back(s); }
        uint32_t hmn = 1, hmx = 1;
        if (!hv.empty()) { hmn = hv[0]; hmx = hv[0]; }
        for (uint32_t v : hv) { if (v > hmx) hmx = v; if (v < hmn) hmn = v; }
        if (hmx == 0) hmx = 1;
        if (hmn == 0) hmn = 1;
        const float cols = 24.0f, rows = 7.0f;
        float s = cw / cols;
        if (ch / rows < s) s = ch / rows;
        float cellW = s, cellH = s;
        if (cellW <= 0 || cellH <= 0) return;
        float gw = cellW * cols, gh = cellH * rows;
        float ox = baseX + (cw - gw) * 0.5f;
        float oy = rect.top + pt + (ch - gh) * 0.5f;
        // 网格底色
        ui_draw_fill_rect(ctx, UiRect{ ox, oy, ox + gw, oy + gh },
                          dark ? rgb255(34, 38, 45) : rgb255(222, 227, 234));
        float gap = std::max(0.5f, s * 0.05f);
        for (int i = 0; i < 168; ++i) {
            int wdi = i / 24, hh = i % 24;
            float gx = ox + hh * cellW + gap;
            float gy = oy + wdi * cellH + gap;
            float gwc = cellW - gap * 2, ghc = cellH - gap * 2;
            if (gwc <= 1 || ghc <= 1) continue;
            uint64_t sum = pts[i].k + pts[i].c;
            UiColor col = sum > 0
                ? heatColor(heatT((uint32_t)sum, hmn, hmx), dark)
                : (dark ? rgb255(40, 44, 52) : rgb255(243, 246, 249));
            float r = 2.0f; if (r > gwc * 0.3f) r = gwc * 0.3f; if (r > ghc * 0.3f) r = ghc * 0.3f;
            ui_draw_fill_rounded_rect(ctx, UiRect{ gx, gy, gx + gwc, gy + ghc }, r, r, col);
        }
        // 左侧行标签：周几（紧贴网格左边缘，宽度固定）
        float labelRight = ox - 4;
        for (int w = 0; w < 7; ++w) {
            std::wstring wl = L"周" + widen(wd[w]);
            float tw = ui_draw_measure_text(ctx, wl.c_str(), 9) + 2;
            UiRect lr = { labelRight - tw, oy + w * cellH, labelRight, oy + (w + 1) * cellH };
            ui_draw_text_ex(ctx, wl.c_str(), lr, axisCol, 9, 2, 0);
        }
        // 底部小时标签（每 6 小时）
        float labBase = oy + gh + 4;
        for (int h = 0; h <= 24; h += 6) {
            wchar_t hb[8];
            _snwprintf_s(hb, _TRUNCATE, L"%d时", h);
            UiRect lr = { ox + h * cellW, labBase, ox + h * cellW + 30, labBase + 14 };
            ui_draw_text_ex(ctx, hb, lr, axisCol, 9, 0, 0);
        }
        // 悬停几何缓存（格宽 = 一小时槽宽；168 项）
        g_trBaseX = ox; g_trSlot = cellW; g_trN = 168;
        g_trTopY = oy; g_trBotY = oy + gh;
    } else {
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
    }

    // 悬停浮窗：命中格/柱时直绘数值框（与鼠标热力图一致；默认放光标左上方）
    if (g_trHover >= 0 && (size_t)g_trHover < n) {
        const TrendPoint& p = pts[(size_t)g_trHover];
        std::wstring wl = widen(p.label);
        if (g_trendMode == 3) wl = L"周" + wl;   // 周标签仅单字，加前缀消歧
        wchar_t tb[96];
        if (g_trendMode == 0) {
            // 按日：两行显示 "HH:00" + "按键X·点击X"
            wchar_t tbh[16];
            _snwprintf_s(tbh, _TRUNCATE, L"%s:00", wl.c_str());
            _snwprintf_s(tb, _TRUNCATE, L"按键 %llu · 点击 %llu",
                         (unsigned long long)p.k, (unsigned long long)p.c);
            float tw1 = ui_draw_measure_text(ctx, tbh, 12);
            float tw2 = ui_draw_measure_text(ctx, tb, 12);
            float tw = tw1 > tw2 ? tw1 : tw2;
            float bw = tw + 20, bh = 36;
            float by = g_trHoverY - bh - 8; if (by < rect.top) by = g_trHoverY + 12;
            float bx = g_trHoverX - bw - 10; if (bx < rect.left) bx = g_trHoverX + 10;
            UiRect br = { bx, by, bx + bw, by + bh };
            ui_draw_fill_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(24, 26, 31, 235));
            ui_draw_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(120, 126, 136, 120), 1.0f);
            ui_draw_text_ex(ctx, tbh, UiRect{ br.left + 10, br.top + 2, br.right - 4, br.top + 16 },
                            rgb255(255, 255, 255), 12, 2, 0);
            ui_draw_text_ex(ctx, tb, UiRect{ br.left + 10, br.top + 16, br.right - 4, br.bottom - 2 },
                            rgb255(255, 255, 255), 12, 2, 0);
        } else if (g_trendMode == 4) {
            _snwprintf_s(tb, _TRUNCATE, L"%s：按键 %llu · 点击 %llu", wl.c_str(),
                         (unsigned long long)p.k, (unsigned long long)p.c);
            float tw = ui_draw_measure_text(ctx, tb, 12);
            float bw = tw + 16, bh = 22;
            float by = g_trHoverY - bh - 8; if (by < rect.top) by = g_trHoverY + 12;
            float bx = g_trHoverX - bw - 10; if (bx < rect.left) bx = g_trHoverX + 10;
            UiRect br = { bx, by, bx + bw, by + bh };
            ui_draw_fill_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(24, 26, 31, 235));
            ui_draw_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(120, 126, 136, 120), 1.0f);
            ui_draw_text_ex(ctx, tb, UiRect{ br.left + 8, br.top, br.right - 4, br.bottom },
                            rgb255(255, 255, 255), 12, 2, 0);
        } else {
            _snwprintf_s(tb, _TRUNCATE, L"%s：按键 %llu · 点击 %llu", wl.c_str(),
                         (unsigned long long)p.k, (unsigned long long)p.c);
            float tw = ui_draw_measure_text(ctx, tb, 12);
            float bw = tw + 16, bh = 22;
            float by = g_trHoverY - bh - 8; if (by < rect.top) by = g_trHoverY + 12;
            float bx = g_trHoverX - bw - 10; if (bx < rect.left) bx = g_trHoverX + 10;
            UiRect br = { bx, by, bx + bw, by + bh };
            ui_draw_fill_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(24, 26, 31, 235));
            ui_draw_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(120, 126, 136, 120), 1.0f);
            ui_draw_text_ex(ctx, tb, UiRect{ br.left + 8, br.top, br.right - 4, br.bottom },
                            rgb255(255, 255, 255), 12, 2, 0);
        }
    }
}

// 总览近24小时强度曲线（每 10 分钟聚合键+点击，144 点折线）
static void ApmDraw(UiWidget, UiDrawCtx ctx, UiRect rect, void*) {
    auto& days = app().days;
    bool dark = (ui_theme_get_mode() == UI_THEME_DARK);
    UiColor axisCol = dark ? rgb255(120, 126, 136) : rgb255(138, 145, 157);

    const int segs = 144;
    std::vector<uint64_t> agg(segs, 0);
    std::vector<uint64_t> aggK(segs, 0);
    std::vector<uint64_t> aggC(segs, 0);
    uint64_t amax = 1;

    SYSTEMTIME st;
    GetLocalTime(&st);
    int nowMinOfDay = st.wHour * 60 + st.wMinute;
    int nowSeg = nowMinOfDay / 10;

    int ty = st.wYear, tm = st.wMonth, td = st.wDay;
    int todayIdx = dayIndexFromYMD(ty, tm, td);
    int yesterdayIdx = todayIdx - 1;

    // 昨天 nowSeg+1..143 → agg[0..142-nowSeg]，今天 0..nowSeg → agg[143-nowSeg..143]
    auto yit = days.find((uint16_t)yesterdayIdx);
    if (yit != days.end()) {
        for (auto& kv : yit->second.minuteActivity) {
            int s = kv.first / 10;
            if (s > nowSeg && s < 144) {
                int idx = s - nowSeg - 1;
                if (idx >= 0 && idx < segs) { agg[idx] += kv.second; if (agg[idx] > amax) amax = agg[idx]; }
            }
        }
        for (auto& kv : yit->second.keyMinuteActivity) {
            int s = kv.first / 10;
            if (s > nowSeg && s < 144) { int idx = s - nowSeg - 1; if (idx >= 0 && idx < segs) aggK[idx] += kv.second; }
        }
        for (auto& kv : yit->second.clickMinuteActivity) {
            int s = kv.first / 10;
            if (s > nowSeg && s < 144) { int idx = s - nowSeg - 1; if (idx >= 0 && idx < segs) aggC[idx] += kv.second; }
        }
    }
    auto tit = days.find((uint16_t)todayIdx);
    if (tit != days.end()) {
        for (auto& kv : tit->second.minuteActivity) {
            int s = kv.first / 10;
            if (s <= nowSeg) {
                int idx = (143 - nowSeg) + s;
                if (idx >= 0 && idx < segs) { agg[idx] += kv.second; if (agg[idx] > amax) amax = agg[idx]; }
            }
        }
        for (auto& kv : tit->second.keyMinuteActivity) {
            int s = kv.first / 10;
            if (s <= nowSeg) { int idx = (143 - nowSeg) + s; if (idx >= 0 && idx < segs) aggK[idx] += kv.second; }
        }
        for (auto& kv : tit->second.clickMinuteActivity) {
            int s = kv.first / 10;
            if (s <= nowSeg) { int idx = (143 - nowSeg) + s; if (idx >= 0 && idx < segs) aggC[idx] += kv.second; }
        }
    }

    float pl = 16, pr = 6, pt = 16, pb = 20;
    float cw = (rect.right - rect.left) - pl - pr;
    float ch = (rect.bottom - rect.top) - pt - pb;
    if (cw <= 0 || ch <= 0) return;
    float baseX = rect.left + pl, baseY = rect.top + pt + ch;

    UiColor gridCol = dark ? rgb255(52, 57, 66) : rgb255(238, 241, 245);
    for (int g = 0; g <= 3; ++g) {
        float gy = baseY - ch * g / 3.0f;
        ui_draw_line(ctx, baseX, gy, baseX + cw, gy, gridCol, 1.0f);
    }
    ui_draw_line(ctx, baseX, baseY, baseX + cw, baseY, gridCol, 1.0f);

    for (int hOff = 0; hOff < 24; hOff += 6) {
        int realH = (st.wHour + hOff + 1) % 24;
        int seg = hOff * 6;
        if (seg > segs) seg = segs;
        wchar_t hl[8];
        _snwprintf_s(hl, _TRUNCATE, L"%d时", realH);
        float lx = baseX + cw * seg / (float)segs;
        UiRect lr = { lx - 16, baseY + 4, lx + 16, baseY + 16 };
        ui_draw_text_ex(ctx, hl, lr, axisCol, 9, 2, 0);
    }

    g_apmBaseX = baseX; g_apmSlot = cw / segs;
    g_apmTopY = rect.top + pt; g_apmBotY = baseY;

    if (amax > 1) {
        UiColor lineCol = rgb255(63, 120, 244);
        float prevX = baseX, prevY = -1;
        for (int i = 0; i < segs; ++i) {
            float fx = baseX + cw * ((i + 0.5f) / segs);
            float fy = baseY - ch * ((float)agg[i] / (float)amax);
            if (i > 0 && prevY >= 0) ui_draw_line(ctx, prevX, prevY, fx, fy, lineCol, 1.5f);
            prevX = fx; prevY = fy;
        }
    }

    // 悬浮浮窗：两行，居中显示，区分按键和点击
    if (g_apmHover >= 0 && g_apmHover < segs) {
        int seg = g_apmHover;
        int min0 = ((st.wHour * 60 + st.wMinute) - (segs - 1 - seg) * 10 + 1440) % 1440;
        int min1 = (min0 + 10) % 1440;
        int h0 = min0 / 60, m0 = min0 % 60;
        int h1 = min1 / 60, m1 = min1 % 60;
        wchar_t tb[80], tb2[80];
        _snwprintf_s(tb, _TRUNCATE, L"%02d:%02d-%02d:%02d", h0, m0, h1, m1);
        uint64_t k = aggK[seg], c = aggC[seg];
        if (k + c == 0 && agg[seg] > 0) {
            // 旧数据无分钟级分离记录：按该小时按键/点击比例估算
            int segDay = (seg <= (143 - nowSeg)) ? yesterdayIdx : todayIdx;
            auto dit = days.find((uint16_t)segDay);
            if (dit != days.end()) {
                uint64_t hk = dit->second.hourlyKeys[h0];
                uint64_t hc = dit->second.hourlyClicks[h0];
                if (hk + hc > 0) { k = agg[seg] * hk / (hk + hc); c = agg[seg] - k; }
                else { k = agg[seg] / 2; c = agg[seg] - k; }
            } else { k = agg[seg] / 2; c = agg[seg] - k; }
        }
        _snwprintf_s(tb2, _TRUNCATE, L"按键 %llu · 点击 %llu", (unsigned long long)k, (unsigned long long)c);
        float tw1 = ui_draw_measure_text(ctx, tb, 12);
        float tw2 = ui_draw_measure_text(ctx, tb2, 12);
        float tw = tw1 > tw2 ? tw1 : tw2;
        float bw = tw + 20, bh = 36;
        float by = g_apmHoverY - bh - 8; if (by < rect.top) by = g_apmHoverY + 12;
        float bx = g_apmHoverX - bw - 10; if (bx < rect.left) bx = g_apmHoverX + 10;
        UiRect br = { bx, by, bx + bw, by + bh };
        ui_draw_fill_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(24, 26, 31, 235));
        ui_draw_rounded_rect(ctx, br, 5.0f, 5.0f, rgb255(120, 126, 136, 120), 1.0f);
        ui_draw_text_ex(ctx, tb, UiRect{ br.left + 10, br.top + 2, br.right - 4, br.top + 16 },
                        rgb255(255, 255, 255), 12, 2, 0);
        ui_draw_text_ex(ctx, tb2, UiRect{ br.left + 10, br.top + 16, br.right - 4, br.bottom - 2 },
                        rgb255(255, 255, 255), 12, 2, 0);
    }
}

// 趋势图悬停：换算鼠标 x → 柱下标，更新悬浮浮窗；越界/离开时清除
static int onTrendMove(UiWidget, float x, float y, int, void*) {
    int idx = -1;
    if (g_trN > 0 && g_trSlot > 0 && x >= g_trBaseX && y >= g_trTopY && y <= g_trBotY) {
        if (g_trendMode == 4) {
            // 时段热力：7行×24列，行列都要参与索引计算
            float cellH = (g_trBotY - g_trTopY) / 7.0f;
            int col = (int)((x - g_trBaseX) / g_trSlot);
            int row = (int)((y - g_trTopY) / cellH);
            if (col >= 0 && col < 24 && row >= 0 && row < 7) idx = row * 24 + col;
        } else {
            int i = (int)((x - g_trBaseX) / g_trSlot);
            if (i >= 0 && (size_t)i < g_trN) idx = i;
        }
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
static int  onApmMove(UiWidget, float, float, int, void*);
static void onApmLeave(UiWidget, void*);
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
static void onApmMount(UiPage, UiWidget w, void*) {
    ui_custom_on_draw(w, ApmDraw, nullptr);
    ui_custom_on_mouse_move(w, onApmMove, nullptr);
    ui_widget_on_mouse_leave(w, onApmLeave, nullptr);
}
// 光标移出趋势图画布 → 收起数值浮窗
static void onTrendLeave(UiWidget, void*) {
    if (g_trHover >= 0) {
        g_trHover = -1;
        if (g_win) ui_window_invalidate(g_win);
    }
}
// APM 曲线悬浮
static int onApmMove(UiWidget, float x, float y, int, void*) {
    int idx = -1;
    if (g_apmSlot > 0 && x >= g_apmBaseX && y >= g_apmTopY && y <= g_apmBotY) {
        int i = (int)((x - g_apmBaseX) / g_apmSlot);
        if (i >= 0 && i < 144) idx = i;
    }
    if (idx != g_apmHover) {
        g_apmHover = idx;
        if (g_win) ui_window_invalidate(g_win);
    }
    g_apmHoverX = x; g_apmHoverY = y;
    return 0;
}
static void onApmLeave(UiWidget, void*) {
    if (g_apmHover >= 0) {
        g_apmHover = -1;
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

// 信息类确认框统一走 core-ui 的 ui_msgbox（主题跟随、居中宿主、Enter/Esc 语义）。
// result: 返回点击的按钮索引（cancel_idx 返回即"取消/关闭"）
static int msgConfirm(const wchar_t* title, const wchar_t* msg,
                      const wchar_t* okText, const wchar_t* cancelText,
                      bool dangerOk = false) {
    const wchar_t* btns[2] = { cancelText, okText };
    UiMsgBoxParams mp = {};
    mp.struct_size = sizeof(mp);
    mp.title = title;
    mp.message = msg;
    mp.buttons = btns;
    mp.button_count = 2;
    mp.default_idx = 1;   // Enter = 主按钮（确认）
    mp.cancel_idx = 0;    // Esc/关闭 = 取消
    mp.icon = UI_MSGBOX_ICON_QUESTION;
    if (dangerOk) {
        UiColor cols[2] = { {0,0,0,0}, {0.72f, 0.13f, 0.09f, 1.0f} };  // 危险操作红色
        mp.button_colors = cols;
    }
    return ui_msgbox_ex(g_win, &mp).button;
}
static void msgInfo(const wchar_t* msg, int icon = UI_MSGBOX_ICON_INFO) {
    const wchar_t* btns[1] = { L"确定" };
    UiMsgBoxParams mp = {};
    mp.struct_size = sizeof(mp);
    mp.title = L"键鼠使用记录";
    mp.message = msg;
    mp.buttons = btns;
    mp.button_count = 1;
    mp.default_idx = 0;
    mp.cancel_idx = 0;
    mp.icon = icon;
    ui_msgbox_ex(g_win, &mp);
}

// 导出备份（KMT 全量）：仅负责文件对话框 + 写盘 + 结果提示
static void doExportBackup() {
    wchar_t file[MAX_PATH] = {};
    wcscpy_s(file, L"KeyMouseTracker-backup.kmt");
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"键鼠使用记录备份 (*.kmt)\0*.kmt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"kmt";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    if (!saveData(file))
        msgInfo(L"导出失败：无法写入目标文件。", UI_MSGBOX_ICON_ERROR);
}

// 导入备份：文件对话框 + ui_msgbox 二次确认（覆盖有风险）
static void doImport() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"键鼠使用记录备份 (*.kmt)\0*.kmt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"kmt";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    if (msgConfirm(L"导入数据", L"导入将覆盖当前全部数据，确定继续？", L"确定", L"取消") != 1) return;
    if (loadData(file)) {
        ensureCurDay();
        app().dirty = true;
        app().needsRefresh = true;
    } else {
        msgInfo(L"导入失败：文件无效或不是有效的数据备份。", UI_MSGBOX_ICON_ERROR);
    }
}

// 解析范围弹窗传来的两个日期字符串；空串=不限。成功返回 true 并写出索引
static bool parseRangeFromUI(const std::string& s1, const std::string& s2,
                             int& startIdx, int& endIdx) {
    int y1 = 0, m1 = 0, d1 = 0, y2 = 0, m2 = 0, d2 = 0;
    bool has1 = parseYMDLoose(s1, 8, y1, m1, d1);
    bool has2 = parseYMDLoose(s2, 8, y2, m2, d2);
    if (!has1 && !has2) { startIdx = 0; endIdx = 65535; return true; }  // 全量
    if (has1) startIdx = dayIndexFromYMD(y1, m1, d1); else startIdx = 0;
    if (has2) endIdx = dayIndexFromYMD(y2, m2, d2); else endIdx = 65535;
    if (startIdx > endIdx) { int t = startIdx; startIdx = endIdx; endIdx = t; }
    return true;
}

// 导出 CSV / JSON：先选文件，再按范围写盘
static void doExportFile(bool csv) {
    wchar_t file[MAX_PATH] = {};
    wcscpy_s(file, csv ? L"KeyMouseTracker-export.csv" : L"KeyMouseTracker-export.json");
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = csv ? L"CSV 表格 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0"
                          : L"JSON 数据 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = csv ? L"csv" : L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    bool ok = csv ? ExportCSV(file, g_pendingStart, g_pendingEnd)
                  : ExportJSON(file, g_pendingStart, g_pendingEnd);
    if (!ok) msgInfo(csv ? L"导出 CSV 失败：无法写入目标文件。" : L"导出 JSON 失败：无法写入目标文件。",
                     UI_MSGBOX_ICON_ERROR);
    else msgInfo(csv ? L"CSV 导出完成。" : L"JSON 导出完成。");
}

// 按范围清除：红色危险确认后执行删除
static void doClearRange() {
    std::wstring msg = L"确定删除该日期范围内的全部统计记录？此操作不可恢复，建议先导出备份。";
    if (msgConfirm(L"清除数据", msg.c_str(), L"删除", L"取消", /*dangerOk*/ true) != 1) return;
    eraseRange(g_pendingStart, g_pendingEnd);
    ensureCurDay();
    saveData(dataFilePath());
    pushStats();
}

// 范围弹窗确认：读取 UI 日期 → 按 pending 用途分发
static void handleRangeConfirm() {
    std::string s1, s2;
    if (char* j = ui_page_get_json(g_page, "rangeStart")) { s1 = jsonText(j, ""); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "rangeEnd"))   { s2 = jsonText(j, ""); ui_page_free(j); }
    int start = 0, end = 65535;
    parseRangeFromUI(s1, s2, start, end);
    g_pendingStart = start; g_pendingEnd = end;
    if (g_pendingReq == 1) doExportFile(true);
    else if (g_pendingReq == 2) doExportFile(false);
    else if (g_pendingReq == 3) doClearRange();
    g_pendingReq = 0;
    if (g_page) ui_page_set_bool(g_page, "rangePickOpen", 0);
}

static void pollCommands() {
    if (!g_page) return;
    int pc = 0, ac = 0, ec = 0, tc = 0, xc = 0, ic = 0, clc = 0;
    int ec2 = 0, ej = 0, rpc = 0, rcc = 0, oac = 0, exc = 0;
    if (char* j = ui_page_get_json(g_page, "pauseCmd")) { pc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "autostartCmd")) { ac = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exitCmd")) { ec = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "themeCmd")) { tc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exportCmd")) { xc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "importCmd")) { ic = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "clearCmd")) { clc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exportCsvCmd")) { ec2 = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exportJsonCmd")) { ej = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "rangePickCmd")) { rpc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "rangeCancelCmd")) { rcc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "optAppCmd")) { oac = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exclCmd")) { exc = jsonInt(j, 0); ui_page_free(j); }

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
        if (!app().days.empty()) doExportBackup();
        else msgInfo(L"暂无数据可导出。");
    }
    if (ic != g_lastImportCmd) {
        g_lastImportCmd = ic;
        doImport();
    }
    // 导出 CSV / JSON：先打开范围选择弹窗（core-ui 内嵌 UI），确认后执行
    if (ec2 != g_lastExportCsvCmd) {
        g_lastExportCsvCmd = ec2;
        g_pendingReq = 1;
        ui_page_set_json(g_page, "rangeKind", "\"csv\"");
        if (g_page) ui_page_set_bool(g_page, "rangePickOpen", 1);
    }
    if (ej != g_lastExportJsonCmd) {
        g_lastExportJsonCmd = ej;
        g_pendingReq = 2;
        ui_page_set_json(g_page, "rangeKind", "\"json\"");
        if (g_page) ui_page_set_bool(g_page, "rangePickOpen", 1);
    }
    // 清除数据：打开范围选择弹窗（可选全部或指定区间）
    if (clc != g_lastClearCmd) {
        g_lastClearCmd = clc;
        g_pendingReq = 3;
        ui_page_set_json(g_page, "rangeKind", "\"clear\"");
        if (g_page) ui_page_set_bool(g_page, "rangePickOpen", 1);
    }
    // 范围弹窗：确认 / 取消
    if (rpc != g_lastRangePickCmd) {
        g_lastRangePickCmd = rpc;
        if (g_pendingReq != 0) handleRangeConfirm();
    }
    if (rcc != g_lastRangeCancelCmd) {
        g_lastRangeCancelCmd = rcc;
        g_pendingReq = 0;
        if (g_page) ui_page_set_bool(g_page, "rangePickOpen", 0);
    }
    // 前台应用统计开关（隐私默认关）
    if (oac != g_lastOptCmd) {
        g_lastOptCmd = oac;
        app().optAppTrack = !app().optAppTrack;
        app().dirty = true;
        pollForeApp();             // 立即刷新前台应用，确保开启后立刻生效
        app().needsRefresh = true;
    }
    // 前台应用排除列表（增量添加）
    if (exc != g_lastExclCmd) {
        g_lastExclCmd = exc;
        std::string raw;
        if (char* j = ui_page_get_json(g_page, "excludeStr")) { raw = jsonText(j, ""); ui_page_free(j); }
        size_t pos = 0;
        while (pos <= raw.size()) {
            size_t comma = raw.find_first_of(",;\n", pos);
            std::string item = raw.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.pop_back();
            while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.erase(item.begin());
            if (!item.empty()) app().excludeApps.insert(item);  // 增量添加
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        app().dirty = true;
        app().needsRefresh = true;
    }
    // 移除单个排除项
    if (char* rj = ui_page_get_json(g_page, "removeExclCmd")) {
        int rc = jsonInt(rj, 0); ui_page_free(rj);
        if (rc != g_lastRemoveExclCmd) {
            g_lastRemoveExclCmd = rc;
            if (char* nj = ui_page_get_json(g_page, "removeExclName")) {
                std::string name = jsonText(nj, ""); ui_page_free(nj);
                if (!name.empty()) { app().excludeApps.erase(name); app().dirty = true; app().needsRefresh = true; }
            }
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
        if (m < 0 || m > 4) m = g_trendMode;
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
    } else if (g_trendMode == 4) { // 本周时段热力：weekStr 为周一日期
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
            // 位移累计：首次采样（-1）仅记录起点，不产生距离
            if (s_last.x >= 0 && s_last.y >= 0) {
                double dx = (double)pt.x - s_last.x, dy = (double)pt.y - s_last.y;
                recordMoveDist((uint64_t)llround(sqrt(dx * dx + dy * dy)));
            }
            recordMove();
            s_last = pt;
        }
        break;
    }
    case TI_SAVE:
        saveData(dataFilePath());
        if (g_page && app().dirty) pushStats();   // 同步存储大小等最新信息
        break;
    case TI_ACTIVE: {
        ensureCurDay();
        DayData& t = app().days[app().cur];
        DWORD now = GetTickCount();
        DWORD since = now - app().lastActivity;
        if (g_sessionDay != app().cur) {            // 跨天：重置活跃段计时
            g_sessionStartTick = 0;
            g_sessionDay = app().cur;
        }
        if (since < 60000) {   // 1 分钟内仍有输入视为连续活跃
            t.activeSec++;
            app().dirty = true;
            if (g_sessionStartTick == 0) {          // 开启新活跃段
                g_sessionStartTick = now;
                t.sessionCount++;
            }
            DWORD seg = (now - g_sessionStartTick) / 1000;
            if (seg > t.maxSessionSec) t.maxSessionSec = seg;
        } else {
            g_sessionStartTick = 0;                 // 连续活跃中断
        }
        break;
    }
    case TI_POLL: {
        pollCommands();
        pollForeApp();                      // 前台应用轮询（仅开启时有效）
        break;
    }
    case TI_REFRESH: {                      // 60 帧 UI 刷新轮询
        if (app().needsRefresh && !g_inResizeMode) {   // 拖拽中不重统计，避免卡顿
            // 节流 + 可见性：合并高频输入（点击/移动）产生的刷新请求；
            // 窗口隐藏（托盘/最小化）时跳过全量 UI 刷新，needsRefresh 保留
            // 待窗口可见后由本分支补刷，避免隐藏期间白做重活。
            bool visible = g_hwnd && IsWindowVisible(g_hwnd);
            if (visible && GetTickCount() - g_lastPushTick >= kPushMinMs) {
                app().needsRefresh = false;
                pushStats();
            }
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
    ui_page_on_widget_mount(g_page, "apm_canvas", onApmMount, nullptr);

    ui_window_on_close_request(g_win, OnCloseRequest, nullptr);
    ui_window_on_resize(g_win, OnWindowResize, nullptr);

    ui_page_set_bool(g_page, "dark", app().darkTheme ? 1 : 0);
    ui_page_set_bool(g_page, "rangePickOpen", 0);   // 范围选择弹窗初始关闭
    {
        char lb[8];
        _snprintf_s(lb, _TRUNCATE, "%d", (int)app().kbLayout);
        ui_page_set_json(g_page, "kbLayout", lb);
    }

    SetWindowSubclass(g_hwnd, SubclassProc, kSubclassId, 0);

    // 安装全局钩子
    if (!InstallHooks()) {
        msgInfo(L"无法安装全局钩子，请以普通进程身份运行。", UI_MSGBOX_ICON_WARNING);
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
    SetTimer(g_hwnd, TI_REFRESH, kRefreshMs, TimerProc);

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
    KillTimer(g_hwnd, TI_REFRESH);
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
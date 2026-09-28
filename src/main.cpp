// 主程序（core-ui 版）：现代界面 + 全局键鼠钩子 + 系统托盘 + 持久化
#include <ui_core.h>
#include "data.h"
#include "timechart.h"   // 统一时间序列图表组件（缩放/平移/十字光标/双轴/平滑）
#include "hooks.h"
#include "autostart.h"
#include "export.h"
#include "api.h"
#include "version.h"
#include "i18n.h"
#include "tray.h"
#include "uiutil.h"
#include "stats_json.h"
#include "heatmap.h"
#include "stats_query.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>   // timeBeginPeriod：提升 WM_TIMER 分辨率，保证 16ms 动画帧稳定
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

#include "app_uix.embed.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")

static UiPage  g_page = 0;
static UiWindow g_win = 0;
static HWND    g_hwnd = nullptr;
static UINT g_taskbarCreatedMsg = 0;   // 系统广播的 TaskbarCreated 消息 id（托盘重建时重加图标）
static int  g_trayRetry = 0;           // 启动初期托盘图标重试次数（开机自启时 Explorer 可能未就绪）
static const UINT_PTR kSubclassId = 0x4B4D54; // "KMT"

// 窗口移动/缩放期间为 true：拖拽中停掉重统计 JSON 与全局重绘，
// 全部重活推迟到 WM_EXITSIZEMOVE 一次性完成，保证拖拽全程不卡顿。
static bool g_inResizeMode = false;

// UI 统计刷新采用局部推送：stats 拆为独立顶层键，pushStats 仅推送内容变化的键
// （快照 diff），避免全量替换触发所有绑定重求值；窗口隐藏时跳过刷新，显示后补刷。
static DWORD g_lastPushTick = 0;
static const DWORD kPushMinMs = 100;  // pushStats 节流：KPI/排行无需 60fps，100ms 足够且避免拖慢图表动画

// pushStats：把当前统计重建为 JSON 并推送到 .uix；定义在下方，SubclassProc 需提前可见
static void pushStats();

// 定时器
enum { TI_SAMPLE = 1, TI_SAVE = 2, TI_ACTIVE = 3, TI_POLL = 4, TI_REFRESH = 5 };
static const UINT kSampleMs = 33;
static const UINT kSaveMs = 30000;
static const UINT kRefreshMs = 16;   // UI 刷新轮询：60 帧（活跃时）；空闲时自适应降至 100ms
static UINT g_curRefreshMs = 16;     // 当前实际刷新频率（SetTimer 动态调整）

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

// —— 前台应用事件驱动归因（替代每 500ms 轮询）——
// EVENT_SYSTEM_FOREGROUND 仅在前台窗口变化时触发；WINEVENT_OUTOFCONTEXT 使回调经主线程
// 消息队列投递，与计时器串行执行，setCurrentForeApp 无竞争。安装失败时回退到 TI_POLL 轮询。
static HWINEVENTHOOK g_foreHook = nullptr;
static void CALLBACK ForegroundProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    pollForeApp();
}
static void InstallForeHook() {
    g_foreHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, ForegroundProc, 0, 0, WINEVENT_OUTOFCONTEXT);
}

// 鼠标移动里程：按 33ms 采样光标位置（GetCursorPos），两点间欧氏距离累加。
// 选择 GetCursorPos 而非 Raw Input：前者跨设备一致（含触摸屏/RDP）、单位是屏幕像素
// （与 distToCm 的 DPI 换算及历史数据同口径）；Raw Input 的 mickey 单位会随指针加速
// 变化、且绝对坐标设备不回传相对位移，导致里程口径不一致（曾因此引入里程失效回归）。
static void SampleMouseMove() {
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
}

static int g_lastPauseCmd = 0;
static int g_lastAutoCmd = 0;
static int g_lastThemeCmd = 0;
static int g_lastLangCmd = 0;
static int g_lastExportCmd = 0;
static int g_lastImportCmd = 0;
static int g_lastClearCmd = 0;
static int g_lastLinkCmd = 0;
static int g_lastExportCsvCmd = 0;
static int g_lastExportJsonCmd = 0;
static int g_lastRangePickCmd = 0;
static int g_lastRangeCancelCmd = 0;

// 范围选择弹窗用途：1=导出CSV 2=导出JSON 3=按范围清除（0=空闲）
static int g_pendingReq = 0;
// 范围弹窗确认后解析出的起止日期索引（导出/清除均使用）
static int g_pendingStart = 0, g_pendingEnd = 65535;

static int g_trendMode = 0;
static int g_trendSelY = 0, g_trendSelM = 0, g_trendSelD = 0;
static int g_statSelY = 0, g_statSelM = 0, g_statSelD = 0;   // 统计模式独立日期（避免时段改日期误触发复位）

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
static size_t g_trN = 0;
// 时段矩阵 scope 与系列（单选）；行列数由绘制写入，move 命中复用
static int g_heatScope = 0;     // 0=日(24×60) 1=周(7×24) 2=月(N×24)
static int g_heatSeries = 0;    // 0=按键 1=点击 2=里程 3=活跃
static int g_heatRows = 24, g_heatCols = 60;

// ---- 统一时间序列图（历史视图 日/月/年 模式）----
static TimeSeriesChart g_trendChart;   // 滚轮缩放/拖拽平移/十字光标/左右双轴
static bool g_trendChartDirty = true;  // 模式或日期变化后需重建视口（懒重建于首次绘制）
// ---- P5：总览 24h 趋势图（D7/Q4 迁移到 TimeSeriesChart，禁缩放/拖拽，固定 [now-24h, now]）----
static TimeSeriesChart g_apmChart;
static std::vector<int> g_overviewSeries;  // 总览叠加系列下标（空=仅 APM 曲线；勾选才画）
static int g_chartWarmup = 0;   // 图表数据切换后的渲染预热帧数：驱动连续重绘数帧，避免首帧冷启动卡顿

// 高精度动画定时器：WM_TIMER 受消息循环繁忙影响会掉到 30ms+，改用多媒体定时器（独立线程、1ms 分辨率）驱动 tick
static UINT g_mmTimerId = 0;
static CRITICAL_SECTION g_chartLock;
static ULONGLONG g_mmLastTick = 0;

static void CALLBACK MMTimerProc(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
    ULONGLONG now = GetTickCount64();
    double dt = 16.0;
    if (g_mmLastTick > 0) {
        dt = (double)(now - g_mmLastTick);
        if (dt > 100.0) dt = 100.0;
        if (dt < 1.0) dt = 1.0;
    }
    g_mmLastTick = now;

    EnterCriticalSection(&g_chartLock);
    bool inv = false;
    if (g_trendChart.animating || g_trendChart.flingVel != 0.0) { g_trendChart.tick(dt); inv = true; }
    if (g_apmChart.animating   || g_apmChart.flingVel   != 0.0) { g_apmChart.tick(dt);   inv = true; }
    LeaveCriticalSection(&g_chartLock);

    if (inv && g_win) ui_window_invalidate(g_win);
}

// 应用排行 / 24h 排行 JSON 较重（遍历全部应用+EMA），节流到 500ms 重建；
// KPI（今日/累计）保持 16ms 实时刷新。缓存避免 pushStats 每帧重算造成卡顿。
static std::string g_appsJsonCache;
static DWORD g_appsCacheTick = 0;
static std::string g_apps24hJsonCache;
static DWORD g_apps24hCacheTick = 0;

// 明细范围/筛选状态见 stats_query.h（g_detail* / g_filter* 已抽离到 stats_query.cpp）
static int g_lastDetailRangeCmd = 0;
static int g_lastDetailRangeCancelCmd = 0;  // 明细范围弹窗取消命令（回落到"全部时间"）

// 活跃状态机（TI_ACTIVE 使用）：连续活跃段计时
static DWORD g_sessionStartTick = 0;   // 当前活跃段起点 tick
static uint16_t g_sessionDay = 0xFFFF; // 会话段归属日（跨天后重置）

// 系统托盘（图标 / 提示 / 菜单 / 命令分派 / 还原主窗口）已抽离到 src/tray.cpp，见 tray.h。

static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR idSubclass, DWORD_PTR) {
    if (idSubclass == kSubclassId) {
        if (msg == WM_SIZE && wp == SIZE_MINIMIZED) {
            if (g_win) ui_window_hide(g_win);
            else ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (msg == kTrayMsg) {
            UINT m = (UINT)lp;
            if (LOWORD(m) == WM_RBUTTONUP || LOWORD(m) == WM_CONTEXTMENU) {
                TrayHandleCommand(TrayShowMenu());
            } else if (LOWORD(m) == WM_LBUTTONDBLCLK) {
                TrayShowWindow();
            }
            return 0;
        }
        if (g_taskbarCreatedMsg && msg == g_taskbarCreatedMsg) {
            // 资源管理器/托盘重建（含开机自启时 Explorer 晚于本程序启动）：重加图标
            TrayAddIcon();
            TrayUpdate(app().paused);
            g_trayRetry = 0;
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

// ==================== 按应用筛选 ====================
// 筛选状态 g_filterApp 与聚合缓存（g_filterKeys/g_filterHeat/g_filterCacheDirty）
// 见 stats_query.h，已抽离到 stats_query.cpp。此处仅保留命令去重计数。
static int g_lastFilterCmd = 0;
static std::string g_hoverApp;         // 悬停的应用（空串=总览），右侧 24h 趋势按此过滤
static int g_lastHoverAppCmd = 0;
static std::string g_pinnedApp;        // 点选固定的应用（空串=未固定）
static int g_lastPinnedAppCmd = 0;

// 统计 JSON 构建（BuildTodayJson / BuildTotalJson / BuildStorageJson / BuildExcludeListJson /
// BuildTopAppsJson / BuildApps24hJson）已抽离到 src/stats_json.cpp，见 stats_json.h。

// 关键约束：core-ui 的 set-trap 无值相等判断，set_json 会让依赖键的绑定全部重求值。
// 故拆为独立顶层键 + 快照 diff，只推送内容变化的键。

// 明细聚合（applyDetailRange / buildDetailJson）已抽离到 src/stats_query.cpp，见 stats_query.h。

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
    // 应用筛选聚合缓存：实时数据持续增长，最多每 500ms 失效一次（绘制时惰性重建）
    if (!g_filterApp.empty() && GetTickCount() - g_filterCacheTick >= 500)
        g_filterCacheDirty = true;
    bool changed = false;
    changed |= pushKeyIfChanged("pausedS",     app().paused ? "true" : "false");
    changed |= pushKeyIfChanged("autostartS",  IsAutoStart() ? "true" : "false");
    changed |= pushKeyIfChanged("optAppTrackS",app().optAppTrack ? "true" : "false");
    changed |= pushKeyIfChanged("todayS",      BuildTodayJson());
    changed |= pushKeyIfChanged("totalS",      BuildTotalJson());
    // 应用排行 / 24h 排行：较重，500ms 节流（KPI 以上已实时）
    if (GetTickCount() - g_appsCacheTick >= 500) {
        g_appsCacheTick = GetTickCount();
        g_appsJsonCache = BuildTopAppsJson();
    }
    // 24h 排行是 stats_json 最贵函数（每 app × 48 桶 × 4 维度聚合），且图表桶宽 10min——
    // 5s 节流精度绰绰有余，较原 500ms 减少 90% 无效重算
    if (GetTickCount() - g_apps24hCacheTick >= 5000) {
        g_apps24hCacheTick = GetTickCount();
        g_apps24hJsonCache = BuildApps24hJson();
    }
    changed |= pushKeyIfChanged("appsS",       g_appsJsonCache);
    changed |= pushKeyIfChanged("apps24hS",    g_apps24hJsonCache);
    changed |= pushKeyIfChanged("storageS",    BuildStorageJson());
    changed |= pushKeyIfChanged("excludeListS",BuildExcludeListJson());
    // P4 明细：仅在历史-明细模式或范围/模式变脏时重算；带缓存 + 5s 周期兜底实时增长
    if (g_trendMode == 1 || g_detailDirty) {
        if (g_detailDirty || GetTickCount() - g_detailCacheTick >= 5000) {
            g_detailCacheTick = GetTickCount();
            g_detailDirty = false;
            g_detailJson = buildDetailJson();
        }
        changed |= pushKeyIfChanged("detailS", g_detailJson);
    }
    // 数据确有变化才请求重绘（热力图/趋势图 canvas 直接读 C++ 内存绘制）
    if (changed && g_win) ui_window_invalidate(g_win);
}

// 键位布局（KeyCell/kKeys/zoneOffset）与热力色阶（heatColor/heatT 等）见 src/heatmap.h。
static const KeyCell* g_khDragLast = nullptr;   // 左键拖拽反转：上一命中的键（同一次拖拽路径内每键只切换一次）

// 键盘热力图：点击按键隐藏/恢复（隐藏后仍计数，仅不显示热度），悬停显示次数。
// 颜色归一化排除已隐藏按键 —— 隐藏/恢复后其余按键热度立即重新分配色阶。
// 绘制时把布局参数写入全局缓存，供鼠标回调换算坐标。
static void KeyHeatDraw(UiWidget, UiDrawCtx ctx, UiRect rect, void*) {
    // 应用筛选：空串走全局累计缓存（零成本），非空走该应用全历史聚合（惰性重建）
    const auto& kc = g_filterApp.empty() ? cumulativeKeys() : keysForApp(g_filterApp);
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
    }
    return 0;   // 键计数已直绘在键上，不再弹 tooltip
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
}

// 鼠标热力图：点击位置累计热力，悬停显示次数，无图例。
static void MouseHeatDraw(UiWidget, UiDrawCtx ctx, UiRect rect, void*) {
    // 应用筛选：空串走全局累计热力缓存，非空走该应用全历史聚合（惰性重建）
    const auto& heat = g_filterApp.empty() ? cumulativeHeat() : heatForApp(g_filterApp);
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
            _snwprintf_s(buf, _TRUNCATE, tr(L"%u 次", L"%u"), c);
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

// 从 dayIndexToStr 结果解析 y/m/d（"YYYY-MM-DD"）
// 解析趋势图选定的年/月/日（由 .uix 日期输入提供；未设置时默认当天）
static void resolveTrendRange(int& y, int& m, int& d) {
    if (g_trendMode == 0) {   // 统计模式：独立日期，避免时段日期变化影响
        if (g_statSelY < 2020) ymdFromDayIndex(app().cur, y, m, d);
        else { y = g_statSelY; m = g_statSelM; d = g_statSelD; }
    } else {
        if (g_trendSelY < 2020) ymdFromDayIndex(app().cur, y, m, d);
        else { y = g_trendSelY; m = g_trendSelM; d = g_trendSelD; }
        if (g_heatScope == 2) d = 1;   // 时段-月只取 y/m
    }
}

// ==================== 统一时间序列图（历史视图 日/月/年） ====================

// 按当前模式/日期重建图表视口（模式或日期变化时置 dirty，首次绘制时懒重建）
// 日期选择后视口以「所选日正午」为中心；span < 24h 时强制扩到 24h，
// 保持 span ≥ 24h 不改变精度；钳制到数据域 [today 00:00, now]（未来无数据时右端贴 now）。
static void trendChartResetViewport() {
    int y = 0, m = 0, d = 0;
    resolveTrendRange(y, m, d);
    // 保留当前视口跨度（滚轮缩放后的精度），未缩放时按 24h 起始
    double curSpan = (g_trendChart.targetSpan > 0) ? g_trendChart.targetSpan : 1440.0;
    double spanM = curSpan < 1440.0 ? 1440.0 : curSpan;   // 强制最小 24h（D4 §4）
    double today0 = 0, nowM = 0, maxData = 0;
    {
        SYSTEMTIME st; GetLocalTime(&st);
        today0 = (double)dayIndexFromYMD(st.wYear, st.wMonth, st.wDay) * 1440.0;
        nowM = today0 + st.wHour * 60.0 + st.wMinute + st.wSecond / 60.0;
        maxData = nowM;                                     // 数据域右端固定到当前时刻
    }
    // 数据域左端 = 最早数据日 00:00（全时域可缩放，可跨天拖拽/缩放到历史任意天）
    double dataMin = today0;
    if (!app().days.empty())
        dataMin = (double)app().days.begin()->first * 1440.0;
    // 目标视口：以所选日正午为中心（D4 §4）
    double t0 = (double)dayIndexFromYMD(y, m, d) * 1440.0 + 720.0 - spanM / 2.0;
    double t1 = (double)dayIndexFromYMD(y, m, d) * 1440.0 + 720.0 + spanM / 2.0;
    // 钳制到数据域 [today0, nowM]：跨度 > 可用范围则取 [today0, nowM] 全量
    if (t1 > nowM) {
        if (t0 >= dataMin) { t0 = nowM - spanM; t1 = nowM; }        // 未来日：右端贴 now
        else                { t0 = dataMin; t1 = nowM; }            // 跨 now 边界：贴满数据域
    }
    if (t0 < dataMin) {
        if (t1 <= maxData) { t0 = dataMin; t1 = dataMin + spanM; }  // 越左边界：贴左
        else                { t0 = dataMin; t1 = nowM; }            // 跨度 > 数据域：贴满
    }
    spanM = t1 - t0;
    g_trendChart.setDataRange(dataMin, nowM);
    // 日期按钮跳转走平滑动画；视口 tStart/span 保持旧值让 stepAnim 追赶
    g_trendChart.targetT = t0;
    g_trendChart.targetSpan = spanM;
    g_trendChart.animating = true;
    g_trendChart.clearCaches();
    g_trendChart.hoverBucket = -1; g_trendChart.hoverActive = false;
    g_trendChart.dragging = false;
    g_trendChart.flingVel = 0.0;
}

// 数据域右端随时间增长：仅推进 dataMax（数据域扩大），不自动左移视口。
// 视口停在用户浏览的位置，新数据出现在右端之外，可手动拖拽/缩放到"现在"。
static void trendChartGrowRange() {
    SYSTEMTIME st; GetLocalTime(&st);
    double nowM = (double)dayIndexFromYMD(st.wYear, st.wMonth, st.wDay) * 1440.0 +
                  st.wHour * 60.0 + st.wMinute + st.wSecond / 60.0;
    if (nowM <= g_trendChart.dataMax) return;
    g_trendChart.dataMax = nowM;
}

// TimeSeriesChart 取数回调：把原始数据按当前桶宽 bw 聚合到可见区间
// 桶 = 全局分钟 [ (first+b)*bw, (first+b+1)*bw )。整日桶走日汇总头（O(1)），
// 局部桶走分钟明细（keyMinuteActivity/clickMinuteActivity/minuteActivity，均与
// optAppTrack 无关、可靠）；无分钟明细的旧数据按 hourlyKeys/hourlyClicks 比例均摊。
// 里程无全局分钟明细，仅 appMin（optAppTrack 开启）有值。只扫描可见区间。
static void trendChartFill(int bw, int64_t firstBucket, int count,
                           std::vector<TSSeries>& out, void*) {
    const int64_t dayM = 1440;
    int64_t dayLo = (int64_t)(g_trendChart.dataMin / dayM);
    int64_t dayHi = (int64_t)((g_trendChart.dataMax - 1) / dayM);
    std::vector<uint64_t> k((size_t)count, 0), c((size_t)count, 0),
                          m((size_t)count, 0), a((size_t)count, 0);
    // 应用筛选：g_filterApp 非空时全部桶改取该应用 appMin 分钟明细聚合
    const std::string& fexe = g_filterApp;
    for (int b = 0; b < count; ++b) {
        int64_t g0 = (firstBucket + (int64_t)b) * (int64_t)bw;
        int64_t g1 = g0 + bw;
        int64_t d0 = g0 / dayM, d1 = (g1 - 1) / dayM;
        if (d0 < dayLo) d0 = dayLo;
        if (d1 > dayHi) d1 = dayHi;
        for (int64_t di = d0; di <= d1; ++di) {
            auto it = app().days.find((uint16_t)di);
            if (it == app().days.end()) continue;
            const DayData& dd = it->second;
            int ms = (int)(g0 - di * dayM), me = (int)(g1 - di * dayM);
            if (ms <= 0 && me >= 1440) {
                // 整日桶：直接取日汇总头；筛选时改取该应用当天空聚合
                if (fexe.empty()) {
                    k[b] += dd.keys; c[b] += dd.clicks;
                    m[b] += dd.distPx;
                    a[b] += dd.activeSec / 60;
                } else {
                    k[b] += appKeys(dd, fexe, 0, 1439);
                    c[b] += appClicks(dd, fexe, 0, 1439);
                    m[b] += appMotionPx(dd, fexe, 0, 1439);
                    a[b] += appActiveMinutes(dd, fexe, 0, 1440);
                }
            } else {
                if (ms < 0) ms = 0; if (me > 1440) me = 1440;
                if (fexe.empty()) {
                    uint64_t sk = 0, sc = 0, sa = 0;
                    auto itk = dd.keyMinuteActivity.lower_bound((uint16_t)ms);
                    for (; itk != dd.keyMinuteActivity.end() && itk->first < me; ++itk) sk += itk->second;
                    auto itc = dd.clickMinuteActivity.lower_bound((uint16_t)ms);
                    for (; itc != dd.clickMinuteActivity.end() && itc->first < me; ++itc) sc += itc->second;
                    if (dd.keyMinuteActivity.empty() && dd.clickMinuteActivity.empty()) {
                        // 无分钟明细的旧数据：按小时比例均摊（忠实 hourlyKeys/hourlyClicks）
                        int h0 = ms / 60, h1 = (me - 1) / 60;
                        for (int h = h0; h <= h1; ++h) {
                            if (h < 0 || h > 23) continue;
                            int hs = h * 60, he = hs + 60;
                            int os = ms > hs ? ms : hs, oe = me < he ? me : he;
                            if (oe > os) {
                                if (dd.hourlyKeys[h]) sk += dd.hourlyKeys[h] * (uint64_t)(oe - os) / 60;
                                if (dd.hourlyClicks[h]) sc += dd.hourlyClicks[h] * (uint64_t)(oe - os) / 60;
                            }
                        }
                    }
                    auto ita = dd.minuteActivity.lower_bound((uint16_t)ms);
                    for (; ita != dd.minuteActivity.end() && ita->first < me; ++ita) ++sa;  // 活跃分钟数
                    k[b] += sk; c[b] += sc;
                    m[b] += appMotionPx(dd, "", ms, me - 1);   // 仅 appMin（optAppTrack 开启）有值
                    a[b] += sa;
                } else {
                    // 按应用：仅遍历该应用 appMin 分钟明细（optAppTrack 开启/迁移后均有）
                    auto itm = dd.appMin.find(fexe);
                    if (itm != dd.appMin.end()) {
                        const AppMinuteData& am = itm->second;
                        for (auto& mm : am.keyByMinute)
                            if (mm.first >= (uint16_t)ms && mm.first < (uint16_t)me)
                                for (auto& kv : mm.second) k[b] += kv.second;
                        for (auto& mm : am.clickByMinute)
                            if (mm.first >= (uint16_t)ms && mm.first < (uint16_t)me)
                                for (auto& g : mm.second) c[b] += g.second;
                        m[b] += appMotionPx(dd, fexe, ms, me - 1);
                        a[b] += appActiveMinutes(dd, fexe, ms, me);
                    }
                }
            }
        }
    }
    out.clear();
    // uint64 累计 → float 桶值（显式转换，避免 C4244 噪音；显示精度足够）
    TSSeries s;
    auto toFloats = [](const std::vector<uint64_t>& src) {
        std::vector<float> f; f.reserve(src.size());
        for (uint64_t v : src) f.push_back((float)v);
        return f;
    };
    // D3 4 轴槽分配（Q7 确认）：按键→L1, 点击→L2, 里程→R1, 活跃→R2
    // 左右各 2 个独立量纲轴，每轴独立 maxVis 动画，刻度文字着色为所属系列色
    s.name = tr(L"按键", L"Keys"); s.axis = 0; s.color = rgb255(63, 120, 244);  s.v = toFloats(k); out.push_back(s);
    s.name = tr(L"点击", L"Clicks"); s.axis = 1; s.color = rgb255(84, 214, 196);  s.v = toFloats(c); out.push_back(s);
    s.name = tr(L"里程", L"Distance"); s.axis = 2; s.color = rgb255(186, 222, 74);   s.v = toFloats(m); out.push_back(s);
    s.name = tr(L"活跃", L"Active"); s.axis = 3; s.color = rgb255(245, 148, 66);  s.v = toFloats(a); out.push_back(s);
}

// ==================== 总览 24h 趋势图（迁移到 TimeSeriesChart） ====================
// 固定窗口 [now-24h, now]、桶 5min、无缩放/拖拽。fill 输出 4 个系列，轴槽与历史一致：
// [0]按键(L1) [1]点击(L2) [2]里程(R1) [3]活跃(R2)（D3 4 轴槽）
// 按 g_overviewSeries 勾选显示（enabled 下标 0..3 直接对应，无 APM 复合系列；全不勾=空白）。
// 旧数据回退（无分钟明细按小时比例均摊）与里程分摊（无 appMin 时按窗口内 APM 强度摊 distPx）
// 逻辑沿用原 ApmDraw。总览恒显示全部应用（不受 g_filterApp 影响）。
static void apmFill(int bw, int64_t firstBucket, int count, std::vector<TSSeries>& out, void*) {
    const int64_t dayM = 1440;
    SYSTEMTIME st; GetLocalTime(&st);
    int64_t nowGM = (int64_t)dayIndexFromYMD(st.wYear, st.wMonth, st.wDay) * dayM +
                    st.wHour * 60 + st.wMinute;
    int64_t dayLo = (nowGM - 1440) / dayM, dayHi = nowGM / dayM;
    std::vector<uint64_t> k((size_t)count, 0), c((size_t)count, 0),
                          m((size_t)count, 0), a((size_t)count, 0);
    // 各覆盖日：窗口内 APM 强度总和 + 当日 distPx（里程回退分摊基数）
    uint64_t dayApm[3] = { 0, 0, 0 }, dayDist[3] = { 0, 0, 0 };
    // 有效筛选应用：悬停预览优先，其次点选固定（悬停时即使已固定也预览悬停项）
    const std::string focusApp = g_hoverApp.empty() ? g_pinnedApp : g_hoverApp;
    for (int64_t di = dayLo; di <= dayHi; ++di) {
        auto it = app().days.find((uint16_t)di);
        if (it != app().days.end()) dayDist[(size_t)(di - dayLo)] = it->second.distPx;
    }
    for (int b = 0; b < count; ++b) {
        int64_t g0 = (firstBucket + (int64_t)b) * (int64_t)bw;
        int64_t g1 = g0 + bw;
        uint64_t sk = 0, sc = 0, sa = 0, sm = 0;
        for (int64_t di = dayLo; di <= dayHi; ++di) {
            auto it = app().days.find((uint16_t)di);
            if (it == app().days.end()) continue;
            const DayData& dd = it->second;
            int ms = (int)(g0 - di * dayM), me = (int)(g1 - di * dayM);
            if (me <= 0 || ms >= dayM) continue;
            if (ms < 0) ms = 0; if (me > dayM) me = dayM;
            if (focusApp.empty()) {
                // 总览：全局分钟明细
                auto itk = dd.keyMinuteActivity.lower_bound((uint16_t)ms);
                for (; itk != dd.keyMinuteActivity.end() && itk->first < me; ++itk) sk += itk->second;
                auto itc = dd.clickMinuteActivity.lower_bound((uint16_t)ms);
                for (; itc != dd.clickMinuteActivity.end() && itc->first < me; ++itc) sc += itc->second;
                auto ita = dd.minuteActivity.lower_bound((uint16_t)ms);
                for (; ita != dd.minuteActivity.end() && ita->first < me; ++ita) ++sa;
                for (auto& ap : dd.appMin) {           // 里程：appMin 分钟像素
                    auto itm = ap.second.movePxByMinute.lower_bound((uint16_t)ms);
                    for (; itm != ap.second.movePxByMinute.end() && itm->first < me; ++itm) sm += itm->second;
                }
            } else {
                // 单应用：仅该应用的 appMin 分钟明细
                auto am = dd.appMin.find(focusApp);
                if (am != dd.appMin.end()) {
                    const AppMinuteData& A = am->second;
                    auto itk = A.keyByMinute.lower_bound((uint16_t)ms);
                    for (; itk != A.keyByMinute.end() && itk->first < me; ++itk)
                        for (auto& kv : itk->second) sk += kv.second;
                    auto itc = A.clickByMinute.lower_bound((uint16_t)ms);
                    for (; itc != A.clickByMinute.end() && itc->first < me; ++itc)
                        for (auto& g : itc->second) sc += g.second;
                    std::set<uint16_t> amins;   // 活跃分钟 = 各子 map 分钟并集
                    for (auto& mm : A.keyByMinute) if (mm.first >= (uint16_t)ms && mm.first < (uint16_t)me) amins.insert(mm.first);
                    for (auto& mm : A.clickByMinute) if (mm.first >= (uint16_t)ms && mm.first < (uint16_t)me) amins.insert(mm.first);
                    for (auto& mm : A.motionByMinute) if (mm.first >= (uint16_t)ms && mm.first < (uint16_t)me) amins.insert(mm.first);
                    for (auto& mm : A.movePxByMinute) if (mm.first >= (uint16_t)ms && mm.first < (uint16_t)me) amins.insert(mm.first);
                    sa += amins.size();
                    auto itm = A.movePxByMinute.lower_bound((uint16_t)ms);
                    for (; itm != A.movePxByMinute.end() && itm->first < me; ++itm) sm += itm->second;
                }
            }
        }
        // 旧数据均摊仅适用于总览（无单应用分钟明细时按全局小时比例摊）；
        // 单应用模式（focusApp 非空）不回落全局，避免把全局数据混入该应用曲线造成异常。
        if (focusApp.empty() && sk == 0 && sc == 0) {
            for (int64_t di = dayLo; di <= dayHi; ++di) {
                auto it = app().days.find((uint16_t)di);
                if (it == app().days.end()) continue;
                const DayData& dd = it->second;
                int ms = (int)(g0 - di * dayM), me = (int)(g1 - di * dayM);
                if (me <= 0 || ms >= dayM) continue;
                if (ms < 0) ms = 0; if (me > dayM) me = dayM;
                int h0 = ms / 60, h1 = (me - 1) / 60;
                for (int h = h0; h <= h1; ++h) {
                    if (h < 0 || h > 23) continue;
                    int hs = h * 60, he = hs + 60;
                    int os = ms > hs ? ms : hs, oe = me < he ? me : he;
                    if (oe > os) {
                        if (dd.hourlyKeys[h]) sk += dd.hourlyKeys[h] * (uint64_t)(oe - os) / 60;
                        if (dd.hourlyClicks[h]) sc += dd.hourlyClicks[h] * (uint64_t)(oe - os) / 60;
                    }
                }
            }
        }
        k[b] = sk; c[b] = sc; a[b] = sa; m[b] = sm;
        uint64_t apm = sk + sc;
        for (int64_t di = dayLo; di <= dayHi; ++di) {
            int ms = (int)(g0 - di * dayM), me = (int)(g1 - di * dayM);
            if (me > 0 && ms < dayM) dayApm[(size_t)(di - dayLo)] += apm;
        }
    }
    // 里程回退：窗口内无 appMin 移动数据时，按各日 distPx 与窗口内该日 APM 强度比例分摊。
    // 仅总览（focusApp 空）执行：单应用模式的里程严格取该应用 movePxByMinute，无则 0。
    uint64_t mTot = 0; for (int i = 0; i < count; ++i) mTot += m[i];
    if (focusApp.empty() && mTot == 0) {
        for (int b = 0; b < count; ++b) {
            int64_t g0 = (firstBucket + (int64_t)b) * (int64_t)bw;
            for (int64_t di = dayLo; di <= dayHi; ++di) {
                int ms = (int)(g0 - di * dayM);
                if (ms < 0 || ms >= dayM) continue;
                size_t o = (size_t)(di - dayLo);
                if (dayDist[o] > 0 && dayApm[o] > 0)
                    m[b] += (uint64_t)((double)dayDist[o] * (double)k[b] / (double)dayApm[o]);
                break;   // g0 只命中一个日
            }
        }
    }
    out.clear();
    auto toFloats = [](const std::vector<uint64_t>& src) {
        std::vector<float> f; f.reserve(src.size());
        for (uint64_t v : src) f.push_back((float)v);
        return f;
    };
    TSSeries s;
    // D3 4 轴槽分配（Q7 确认，与历史统计表一致）：按键→L1, 点击→L2, 里程→R1, 活跃→R2
    s.name = tr(L"按键", L"Keys"); s.axis = 0; s.color = rgb255(63, 120, 244);  s.v = toFloats(k); out.push_back(s);
    s.name = tr(L"点击", L"Clicks"); s.axis = 1; s.color = rgb255(84, 214, 196);  s.v = toFloats(c); out.push_back(s);
    s.name = tr(L"里程", L"Distance"); s.axis = 2; s.color = rgb255(186, 222, 74);   s.v = toFloats(m); out.push_back(s);
    s.name = tr(L"活跃", L"Active"); s.axis = 3; s.color = rgb255(245, 148, 66);  s.v = toFloats(a); out.push_back(s);
}

// 日/月/年 统一可缩放图表：懒重建视口后委托组件绘制
static void trendChartDraw(UiDrawCtx ctx, UiRect rect) {
    if (g_trendChartDirty) {
        trendChartResetViewport();
        g_trendChartDirty = false;
    }
    trendChartGrowRange();   // 数据域右端随当前时刻推进（数据域扩大，视口不动）
    if (!g_trendChart.fill) {
        g_trendChart.fill = trendChartFill;
        g_trendChart.ud = nullptr;
        g_trendChart.line = true;   // 默认折线（与总览 24h 曲线一致；trendChartType 轮询可切回柱状）
        if (g_trendChart.enabled.empty())
            g_trendChart.enabled = { 0, 1 };   // 默认仅显示 按键+点击（UI trendSeries 轮询会覆盖）
    }
    EnterCriticalSection(&g_chartLock);
    g_trendChart.draw(ctx, rect, ui_theme_get_mode() == UI_THEME_DARK);
    LeaveCriticalSection(&g_chartLock);
}

// ==================== 时段矩阵（1440 格日视图 / 7×24 周 / N×24 月） ====================
// 系列单选（g_heatSeries：0=按键 1=点击 2=里程 3=活跃），横轴=时间维，纵轴=天/时刻维。
// 悬停浮窗仅显示「数值 + 单位」，不显示格坐标长标签。
static void trendHeatDraw(UiDrawCtx ctx, UiRect rect) {
    bool dark = (ui_theme_get_mode() == UI_THEME_DARK);
    UiColor axisCol = dark ? rgb255(120, 126, 136) : rgb255(138, 145, 157);
    int y = 0, m = 0, d = 0;
    resolveTrendRange(y, m, d);
    int rows, cols;
    if (g_heatScope == 0)      { rows = 24; cols = 60; }   // 日：24 行(小时) × 60 列(分钟) = 1440 格
    else if (g_heatScope == 1) { rows = 7;  cols = 24; }   // 周：7 行(周几) × 24 列(小时)
    else                       { rows = 12; cols = daysInMonth(y, m); } // 月：12 行(每行 2h) × N 列(日)（横纵调换）
    int n = rows * cols;
    int base = dayIndexFromYMD(y, m, d);   // 日/周=起始日（周一）；月=1 日
    std::vector<uint64_t> cell((size_t)n, 0);
    // 周/月单元格索引：周=行(天)×24+列(小时)；月=行(2h桶)×cols+列(天)（横纵调换）
    auto heatCellIdx = [&](int dayIdx, int hour) -> size_t {
        return (g_heatScope == 1) ? (size_t)dayIdx * 24 + (size_t)hour
                                  : (size_t)hour * (size_t)cols + (size_t)dayIdx;
    };
    // 月视图把小时归入 2h 桶（行下标 = hour/2）；周视图行下标 = hour 本身
    auto rowOf = [](int h) { return g_heatScope == 2 ? h / 2 : h; };

    // ---- 数据填充（键/点/活跃走全局分钟图恒可靠；里程仅 appMin，需 optAppTrack）----
    // 应用过滤（g_filterApp 非空）时键/点/里程均改取该应用 appMin 分钟明细（与旧 7×24 一致）
    if (g_heatScope == 0) {
        auto it = app().days.find((uint16_t)base);
        if (it != app().days.end()) {
            const DayData& dd = it->second;
            if (g_heatSeries == 0) {
                if (g_filterApp.empty()) {
                    for (auto& kv : dd.keyMinuteActivity)
                        if (kv.first < 1440) cell[(size_t)(kv.first / 60) * 60 + (kv.first % 60)] += kv.second;
                } else {
                    auto am = dd.appMin.find(g_filterApp);
                    if (am != dd.appMin.end())
                        for (auto& mm : am->second.keyByMinute)
                            if (mm.first < 1440)
                                for (auto& kv : mm.second) cell[(size_t)(mm.first / 60) * 60 + (mm.first % 60)] += kv.second;
                }
            } else if (g_heatSeries == 1) {
                if (g_filterApp.empty()) {
                    for (auto& kv : dd.clickMinuteActivity)
                        if (kv.first < 1440) cell[(size_t)(kv.first / 60) * 60 + (kv.first % 60)] += kv.second;
                } else {
                    auto am = dd.appMin.find(g_filterApp);
                    if (am != dd.appMin.end())
                        for (auto& mm : am->second.clickByMinute)
                            if (mm.first < 1440)
                                for (auto& g : mm.second) cell[(size_t)(mm.first / 60) * 60 + (mm.first % 60)] += g.second;
                }
            } else if (g_heatSeries == 3) {
                if (g_filterApp.empty()) {
                    for (auto& kv : dd.minuteActivity)   // 活跃：该分钟是否有活动（0/1）
                        if (kv.first < 1440) cell[(size_t)(kv.first / 60) * 60 + (kv.first % 60)] = 1;
                } else {
                    auto am = dd.appMin.find(g_filterApp);
                    if (am != dd.appMin.end()) {
                        auto mark = [&](uint16_t m) { if (m < 1440) cell[(size_t)(m / 60) * 60 + (m % 60)] = 1; };
                        for (auto& kv : am->second.keyByMinute) mark(kv.first);
                        for (auto& kv : am->second.clickByMinute) mark(kv.first);
                        for (auto& kv : am->second.motionByMinute) mark(kv.first);
                        for (auto& kv : am->second.movePxByMinute) mark(kv.first);
                        for (auto& kv : am->second.clickBtnMinute) mark(kv.first);
                    }
                }
            } else { // 2 里程：分钟像素累加（无前台应用统计时恒 0）
                if (g_filterApp.empty()) {
                    for (auto& ap : dd.appMin)
                        for (auto& mm : ap.second.movePxByMinute)
                            if (mm.first < 1440) cell[(size_t)(mm.first / 60) * 60 + (mm.first % 60)] += mm.second;
                } else {
                    auto am = dd.appMin.find(g_filterApp);
                    if (am != dd.appMin.end())
                        for (auto& mm : am->second.movePxByMinute)
                            if (mm.first < 1440) cell[(size_t)(mm.first / 60) * 60 + (mm.first % 60)] += mm.second;
                }
            }
        }
    } else {
        int dayCount = (g_heatScope == 1) ? rows : cols;   // 周=7 天，月=daysInMonth 天
        for (int di = 0; di < dayCount; ++di) {   // 周/月：逐天，列=小时（周）或行=小时（月）
            auto it = app().days.find((uint16_t)(base + di));
            if (it == app().days.end()) continue;
            const DayData& dd = it->second;
            if (g_heatSeries == 0) {
                if (g_filterApp.empty()) {
                    for (int h = 0; h < 24; ++h) cell[heatCellIdx(di, rowOf(h))] += dd.hourlyKeys[h];
                } else {
                    auto am = dd.appMin.find(g_filterApp);
                    if (am != dd.appMin.end())
                        for (auto& mm : am->second.keyByMinute) {
                            int h = mm.first / 60;
                            if (h < 24)
                                for (auto& kv : mm.second) cell[heatCellIdx(di, rowOf(h))] += kv.second;
                        }
                }
            } else if (g_heatSeries == 1) {
                if (g_filterApp.empty()) {
                    for (int h = 0; h < 24; ++h) cell[heatCellIdx(di, rowOf(h))] += dd.hourlyClicks[h];
                } else {
                    auto am = dd.appMin.find(g_filterApp);
                    if (am != dd.appMin.end())
                        for (auto& mm : am->second.clickByMinute) {
                            int h = mm.first / 60;
                            if (h < 24)
                                for (auto& g : mm.second) cell[heatCellIdx(di, rowOf(h))] += g.second;
                        }
                }
            } else if (g_heatSeries == 3) {
                if (g_filterApp.empty()) {
                    for (auto& kv : dd.minuteActivity) {   // 活跃分钟数
                        int h = kv.first / 60;
                        if (h < 24) cell[heatCellIdx(di, rowOf(h))] += 1;
                    }
                } else {
                    auto am = dd.appMin.find(g_filterApp);
                    if (am != dd.appMin.end()) {
                        std::set<uint16_t> mins;
                        auto add = [&](const auto& m) { for (auto& kv : m) mins.insert(kv.first); };
                        add(am->second.keyByMinute);
                        add(am->second.clickByMinute);
                        add(am->second.motionByMinute);
                        add(am->second.movePxByMinute);
                        add(am->second.clickBtnMinute);
                        for (uint16_t m : mins) { int h = m / 60; if (h < 24) cell[heatCellIdx(di, rowOf(h))] += 1; }
                    }
                }
            } else { // 2 里程
                if (g_filterApp.empty()) {
                    for (auto& ap : dd.appMin)
                        for (auto& mm : ap.second.movePxByMinute) {
                            int h = mm.first / 60;
                            if (h < 24) cell[heatCellIdx(di, rowOf(h))] += mm.second;
                        }
                } else {
                    auto am = dd.appMin.find(g_filterApp);
                    if (am != dd.appMin.end())
                        for (auto& mm : am->second.movePxByMinute) {
                            int h = mm.first / 60;
                            if (h < 24) cell[heatCellIdx(di, rowOf(h))] += mm.second;
                        }
                }
            }
        }
    }

    // ---- 归一化：可见非零 min..max 之间 log 插值（与键盘/鼠标热力图一致）----
    uint32_t hmn = 1, hmx = 1;
    {
        bool first = true;
        for (int i = 0; i < n; ++i) {
            uint32_t v = (uint32_t)cell[i];
            if (v == 0) continue;
            if (first) { hmn = hmx = v; first = false; }
            else { if (v > hmx) hmx = v; if (v < hmn) hmn = v; }
        }
        if (hmx == 0) hmx = 1;
        if (hmn == 0) hmn = 1;
    }

    // ---- 布局与网格（每格正方形，网格居中）----
    float pl = 30, pr = 8, pt = 12, pb = 24;
    float cw = (rect.right - rect.left) - pl - pr;
    float ch = (rect.bottom - rect.top) - pt - pb;
    if (cw <= 10 || ch <= 10) return;
    float cellSide = std::min(cw / cols, ch / rows);   // 正方形边长
    if (cellSide <= 0.5f) return;
    float gw = cellSide * cols, gh = cellSide * rows;  // 实际网格尺寸
    float ox = rect.left + pl + (cw - gw) / 2;     // 水平居中
    float oy = rect.top + pt + (ch - gh) / 2;      // 垂直居中
    float cellW = cellSide, cellH = cellSide;

    ui_draw_fill_rect(ctx, UiRect{ ox, oy, ox + gw, oy + gh },
                      dark ? rgb255(34, 38, 45) : rgb255(222, 227, 234));
    float gap = std::max(0.5f, std::min(cellW, cellH) * 0.08f);
    for (int i = 0; i < n; ++i) {
        int r = i / cols, c = i % cols;
        float gx = ox + c * cellW + gap;
        float gy = oy + r * cellH + gap;
        float gwc = cellW - gap * 2, ghc = cellH - gap * 2;
        if (gwc <= 1 || ghc <= 1) continue;
        uint64_t v = cell[i];
        UiColor col = v > 0
            ? heatColor(heatT((uint32_t)v, hmn, hmx), dark)
            : (dark ? rgb255(40, 44, 52) : rgb255(243, 246, 249));
        float rr = 2.0f; if (rr > gwc * 0.3f) rr = gwc * 0.3f; if (rr > ghc * 0.3f) rr = ghc * 0.3f;
        ui_draw_fill_rounded_rect(ctx, UiRect{ gx, gy, gx + gwc, gy + ghc }, rr, rr, col);
    }

    // ---- 行标签（日=小时 / 周=周几 / 月=日号）----
    // 三种 scope 统一使用 ui_draw_measure_text 精确右对齐到网格左缘（ox-4），
    // 避免"日/月"下 label 与 grid 的相对位置随 grid 缩放漂移、视觉上被推到窗格与页边居中。
    // 月/日：仅每 2h 一行（p6），避免 24 行 label 密集遮挡网格。
    float labelRight = ox - 4;
    if (g_heatScope == 0) {
        for (int h = 0; h < 24; h += 2) {   // 每 2 小时一个时钟标签（消除锯齿、可读性提升）
            wchar_t hb[8];
            _snwprintf_s(hb, _TRUNCATE, tr(L"%d时", L"%dh"), h);
            float tw = ui_draw_measure_text(ctx, hb, 9) + 2;
            UiRect lr = { labelRight - tw, oy + h * cellH, labelRight, oy + h * cellH + cellH };
            ui_draw_text_ex(ctx, hb, lr, axisCol, 9, 2, 0);
        }
    } else if (g_heatScope == 1) {
        static const char* wd[] = { "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89",
                                    "\xE5\x9B\x9B", "\xE4\xBA\x94", "\xE5\x85\xAD",
                                    "\xE6\x97\xA5" };   // 一二三四五六日
        static const char* enWd[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
        for (int w = 0; w < 7; ++w) {
            std::wstring wl = tr(L"周", L"") + widen(app().lang == 1 ? enWd[w] : wd[w]);
            float tw = ui_draw_measure_text(ctx, wl.c_str(), 9) + 2;
            UiRect lr = { labelRight - tw, oy + w * cellH, labelRight, oy + (w + 1) * cellH };
            ui_draw_text_ex(ctx, wl.c_str(), lr, axisCol, 9, 2, 0);
        }
    } else {
        // 月（横纵调换）：行=2小时桶（0..11），每行标起始小时
        for (int h = 0; h < rows; ++h) {
            wchar_t hb[8];
            _snwprintf_s(hb, _TRUNCATE, tr(L"%d时", L"%dh"), h * 2);
            float tw = ui_draw_measure_text(ctx, hb, 9) + 2;
            UiRect lr = { labelRight - tw, oy + h * cellH, labelRight, oy + h * cellH + cellH };
            ui_draw_text_ex(ctx, hb, lr, axisCol, 9, 2, 0);
        }
    }

    // ---- 列标签（日=每 10 分钟 / 周=每 6 小时 / 月=日号）----
    float labBase = oy + gh + 4;
    if (g_heatScope == 0) {
        for (int mn = 0; mn <= 50; mn += 10) {
            wchar_t mb[8];
            _snwprintf_s(mb, _TRUNCATE, tr(L"%d分", L"%dm"), mn);
            UiRect lr = { ox + mn * cellW, labBase, ox + mn * cellW + 26, labBase + 14 };
            ui_draw_text_ex(ctx, mb, lr, axisCol, 9, 0, 0);
        }
    } else if (g_heatScope == 1) {
        for (int h = 0; h <= 24; h += 6) {
            wchar_t hb[8];
            _snwprintf_s(hb, _TRUNCATE, tr(L"%d时", L"%dh"), h);
            UiRect lr = { ox + h * cellW, labBase, ox + h * cellW + 30, labBase + 14 };
            ui_draw_text_ex(ctx, hb, lr, axisCol, 9, 0, 0);
        }
    } else {
        // 月：列=日号（1-N，抽稀显示）
        int step = cols / 12 + 1;
        for (int dd = 0; dd < cols; dd += step) {
            wchar_t db[8];
            _snwprintf_s(db, _TRUNCATE, L"%d", dd + 1);
            UiRect lr = { ox + dd * cellW, labBase, ox + dd * cellW + 20, labBase + 14 };
            ui_draw_text_ex(ctx, db, lr, axisCol, 9, 0, 0);
        }
    }

    // ---- 悬停几何缓存（move 命中复用）----
    g_trBaseX = ox; g_trSlot = cellW; g_trN = (size_t)n;
    g_trTopY = oy; g_trBotY = oy + gh;
    g_heatRows = rows; g_heatCols = cols;

    // ---- 悬停浮窗：仅「数值 + 单位」----
    if (g_trHover >= 0 && (size_t)g_trHover < (size_t)n) {
        uint64_t v = cell[(size_t)g_trHover];
        if (v > 0) {
            wchar_t tb[40];
            if (g_heatSeries == 2) {
                double cm = distToCm(v);                       // 里程：px → m（1 位小数）
                _snwprintf_s(tb, _TRUNCATE, L"%.1f m", cm / 100.0);
            } else if (g_heatSeries == 3) {
                _snwprintf_s(tb, _TRUNCATE, L"%llu min", (unsigned long long)v);
            } else {
                _snwprintf_s(tb, _TRUNCATE, tr(L"%llu 次", L"%llu"), (unsigned long long)v);
            }
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

static void TrendDraw(UiWidget, UiDrawCtx ctx, UiRect rect, void*) {
    // 0=统计表（统一可缩放时间序列图），1=明细（.uix 渲染 detailS，canvas 隐藏），2=时段矩阵
    if (g_trendMode == 0) { trendChartDraw(ctx, rect); return; }
    if (g_trendMode == 2) { trendHeatDraw(ctx, rect); return; }
}

// 总览 24h 键鼠活动趋势（D7/迁移到 TimeSeriesChart，无缩放/拖拽）。
// 固定窗口 [now-24h, now]，桶 5min（fixedBw），折线模式延续旧版 APM 曲线风格；
// 勾选系列由 g_overviewSeries 轮询映射到 g_apmChart.enabled（下标 0..3 直接对应，无 APM 复合）。
static void ApmDraw(UiWidget, UiDrawCtx ctx, UiRect rect, void*) {
    SYSTEMTIME st; GetLocalTime(&st);
    double nowM = (double)dayIndexFromYMD(st.wYear, st.wMonth, st.wDay) * 1440.0 +
                  st.wHour * 60.0 + st.wMinute;
    // 固定窗口 [now-24h, now]；左右各留一桶（10min）余量，折线端点不被绘图区边缘裁剪
    const double apmPad = 10.0;   // fixedBw=10 的一桶
    if (!g_apmChart.fill) {
        g_apmChart.fill = apmFill;
        g_apmChart.line = true;      // 折线（延续旧版 APM 曲线风格）
        g_apmChart.fixedBw = 10;     // 10min 桶（24h → 144 桶，精度适中，绘制更轻）
        // enabled 由 pollCommands 轮询 overviewSeries 设置（默认按键）；此处不覆盖，避免时序竞争
        g_apmChart.setDataRange(0, 1.0e9);
        g_apmChart.tStart = nowM - 1440.0 - apmPad;
        g_apmChart.span = 1440.0 + 2.0 * apmPad;
    }
    // 固定窗口随当前时刻滚动（保持动态滚动显示 24h 变化）
    if (nowM - 1440.0 - apmPad != g_apmChart.tStart) {
        g_apmChart.tStart = nowM - 1440.0 - apmPad;
        g_apmChart.span = 1440.0 + 2.0 * apmPad;
        // 跨桶（≥5min）时 ensureData 的缓存键自然失效；此处不做强制清理，避免每分钟重算
    }
    EnterCriticalSection(&g_chartLock);
    g_apmChart.draw(ctx, rect, ui_theme_get_mode() == UI_THEME_DARK);
    LeaveCriticalSection(&g_chartLock);
}

// 趋势图悬停：统计表走 TimeSeriesChart 拖拽/十字光标；时段矩阵换算格下标；越界/离开时清除
static int onTrendMove(UiWidget, float x, float y, int, void*) {
    // 统计表模式（0）：拖拽平移优先，其次十字光标悬停
    if (g_trendMode == 0) {
        if (g_trendChart.dragging) {
            bool inside = x >= g_trendChart.plotX0 && x <= g_trendChart.plotX1 &&
                          y >= g_trendChart.plotY0 && y <= g_trendChart.plotY1;
            g_trendChart.onDrag(x, inside);
            if (g_win) ui_window_invalidate(g_win);
        } else {
            bool ch = g_trendChart.onMoveInside(x, y);
            if (ch && g_win) ui_window_invalidate(g_win);
        }
        return 0;
    }
    // 时段矩阵（2）：行列按当前 scope 命中格下标（明细模式 canvas 未挂载，无事件）
    int idx = -1;
    if (g_trN > 0 && g_trSlot > 0 && x >= g_trBaseX && y >= g_trTopY && y <= g_trBotY) {
        float cellH = (g_trBotY - g_trTopY) / (float)g_heatRows;
        int col = (int)((x - g_trBaseX) / g_trSlot);
        int row = (int)((y - g_trTopY) / cellH);
        if (col >= 0 && col < g_heatCols && row >= 0 && row < g_heatRows) idx = row * g_heatCols + col;
    }
    if (idx != g_trHover) {
        g_trHover = idx;
        if (g_win) ui_window_invalidate(g_win);
    }
    g_trHoverX = x; g_trHoverY = y;
    return 0;
}

// 统计表模式滚轮缩放（光标锚点）。delta 正=上滚=放大（跨度缩小 1/1.2）
static void onTrendWheel(UiWidget, float x, float y, float delta, void*) {
    if (g_trendMode != 0) return;
    float dw = g_trendChart.plotX1 - g_trendChart.plotX0;
    if (dw <= 0) return;
    // 锚点时间：鼠标 x 映射到时间轴 value
    double vMin = g_trendChart.tStart +
                  (double)(x - g_trendChart.plotX0) / (double)dw * g_trendChart.span;
    double factor = (delta > 0) ? (1.0 / 1.2) : 1.2;
    g_trendChart.zoomAt(vMin, factor);
    if (g_win) ui_window_invalidate(g_win);
}
// 统计表模式拖拽平移：按下记录起点，移动改变视口起点
static int onTrendDown(UiWidget, float x, float, int, void*) {
    if (g_trendMode == 0) g_trendChart.onDown(x);
    return 0;
}
static int onTrendUp(UiWidget, float x, float y, int, void*) {
    g_trendChart.onUp();
    // 拖拽结束后立即按当前光标重新吸附十字光标，避免残留旧桶高亮
    if (g_trendMode == 0) {
        bool ch = g_trendChart.onMoveInside(x, y);
        if (ch && g_win) ui_window_invalidate(g_win);
    }
    return 0;
}

static void onMouseHeatLeave(UiWidget, void*);
static void onTrendLeave(UiWidget, void*);
static int  onApmMove(UiWidget, float, float, int, void*);
static void onApmLeave(UiWidget, void*);
static void onKeyHeatMount(UiPage, UiWidget w, void*) {
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
    ui_custom_on_mouse_down(w, onTrendDown, nullptr);
    ui_custom_on_mouse_up(w, onTrendUp, nullptr);
    // 注意：ui_custom_on_mouse_wheel 对 CustomWidget 收不到事件（core-ui 分发只认
    // TextArea/ImageView/ScrollView 等），必须用 widget 级 ui_widget_on_mouse_wheel
    ui_widget_on_mouse_wheel(w, onTrendWheel, nullptr);
    ui_widget_on_mouse_leave(w, onTrendLeave, nullptr);
}
static void onApmMount(UiPage, UiWidget w, void*) {
    ui_custom_on_draw(w, ApmDraw, nullptr);
    ui_custom_on_mouse_move(w, onApmMove, nullptr);
    ui_widget_on_mouse_leave(w, onApmLeave, nullptr);
}
// 光标移出趋势图画布 → 收起数值浮窗
static void onTrendLeave(UiWidget, void*) {
    bool ch = false;
    if (g_trHover >= 0) { g_trHover = -1; ch = true; }
    if (g_trendChart.hoverActive || g_trendChart.hoverBucket >= 0) {
        g_trendChart.onLeave(); ch = true;
    }
    if (ch && g_win) ui_window_invalidate(g_win);
}
// APM 曲线悬浮（P5：委托 TimeSeriesChart 十字光标/浮窗，无拖拽缩放）
static int onApmMove(UiWidget, float x, float y, int, void*) {
    bool ch = g_apmChart.onMoveInside(x, y);
    if (ch && g_win) ui_window_invalidate(g_win);
    return 0;
}
static void onApmLeave(UiWidget, void*) {
    if (g_apmChart.hoverActive || g_apmChart.hoverBucket >= 0) {
        g_apmChart.onLeave();
        if (g_win) ui_window_invalidate(g_win);
    }
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
    const wchar_t* btns[1] = { tr(L"确定", L"OK") };
    UiMsgBoxParams mp = {};
    mp.struct_size = sizeof(mp);
    mp.title = tr(L"键鼠使用记录", L"KeyMouseTracker");
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
    ofn.lpstrFilter = tr(L"键鼠使用记录备份 (*.kmt)\0*.kmt\0所有文件 (*.*)\0*.*\0",
                         L"KeyMouseTracker backup (*.kmt)\0*.kmt\0All files (*.*)\0*.*\0");
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"kmt";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    if (!saveData(file))
        msgInfo(tr(L"导出失败：无法写入目标文件。", L"Export failed: unable to write file."), UI_MSGBOX_ICON_ERROR);
}

// 导入备份：文件对话框 + ui_msgbox 二次确认（覆盖有风险）
static void doImport() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = tr(L"键鼠使用记录备份 (*.kmt)\0*.kmt\0所有文件 (*.*)\0*.*\0",
                         L"KeyMouseTracker backup (*.kmt)\0*.kmt\0All files (*.*)\0*.*\0");
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"kmt";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    if (msgConfirm(tr(L"导入数据", L"Import Data"),
                   tr(L"导入将覆盖当前全部数据，确定继续？", L"Importing will overwrite all current data. Continue?"),
                   tr(L"确定", L"OK"), tr(L"取消", L"Cancel")) != 1) return;
    if (loadData(file)) {
        ensureCurDay();
        app().dirty = true;
        app().needsRefresh = true;
    } else {
        msgInfo(tr(L"导入失败：文件无效或不是有效的数据备份。", L"Import failed: invalid or unrecognized data file."), UI_MSGBOX_ICON_ERROR);
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
    ofn.lpstrFilter = csv ? tr(L"CSV 表格 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0",
                                L"CSV table (*.csv)\0*.csv\0All files (*.*)\0*.*\0")
                          : tr(L"JSON 数据 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0",
                                L"JSON data (*.json)\0*.json\0All files (*.*)\0*.*\0");
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = csv ? L"csv" : L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    bool ok = csv ? ExportCSV(file, g_pendingStart, g_pendingEnd)
                  : ExportJSON(file, g_pendingStart, g_pendingEnd);
    if (!ok) msgInfo(csv ? tr(L"导出 CSV 失败：无法写入目标文件。", L"Export CSV failed: unable to write file.")
                         : tr(L"导出 JSON 失败：无法写入目标文件。", L"Export JSON failed: unable to write file."),
                     UI_MSGBOX_ICON_ERROR);
    else msgInfo(csv ? tr(L"CSV 导出完成。", L"CSV export complete.")
                     : tr(L"JSON 导出完成。", L"JSON export complete."));
}

// 按范围清除：红色危险确认后执行删除
static void doClearRange() {
    std::wstring msg = tr(L"确定删除该日期范围内的全部统计记录？此操作不可恢复，建议先导出备份。",
                          L"Delete all records in this date range? This cannot be undone; export a backup first.");
    if (msgConfirm(tr(L"清除数据", L"Clear Data"), msg.c_str(),
                   tr(L"删除", L"Delete"), tr(L"取消", L"Cancel"), /*dangerOk*/ true) != 1) return;
    eraseRange(g_pendingStart, g_pendingEnd);
    ensureCurDay();
    saveData(dataFilePath());
    // 删除头部（最早）数据后数据域左端前移：重建趋势视口 + 清缓存，移除原无数据段落
    g_trendChartDirty = true;
    g_trendChart.clearCaches();
    g_apmChart.clearCaches();
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
    int pc = 0, ac = 0, tc = 0, lc = 0, xc = 0, ic = 0, clc = 0;
    int ec2 = 0, ej = 0, rpc = 0, rcc = 0, exc = 0;
    if (char* j = ui_page_get_json(g_page, "pauseCmd")) { pc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "autostartCmd")) { ac = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "themeCmd")) { tc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "langCmd")) { lc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exportCmd")) { xc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "importCmd")) { ic = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "clearCmd")) { clc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exportCsvCmd")) { ec2 = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exportJsonCmd")) { ej = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "rangePickCmd")) { rpc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "rangeCancelCmd")) { rcc = jsonInt(j, 0); ui_page_free(j); }
    if (char* j = ui_page_get_json(g_page, "exclCmd")) { exc = jsonInt(j, 0); ui_page_free(j); }

    if (pc != g_lastPauseCmd) {
        g_lastPauseCmd = pc;
        app().paused = !app().paused;
        app().dirty = true;
        app().needsRefresh = true;
        TrayUpdate(app().paused);
        TrayRefreshCheck();
    }
    if (ac != g_lastAutoCmd) {
        g_lastAutoCmd = ac;
        SetAutoStart(!IsAutoStart());
        app().needsRefresh = true;
        TrayRefreshCheck();
    }
    if (tc != g_lastThemeCmd) {
        g_lastThemeCmd = tc;
        app().darkTheme = !app().darkTheme;
        app().dirty = true;
        ui_theme_set_mode(app().darkTheme ? UI_THEME_DARK : UI_THEME_LIGHT);
        if (g_page) ui_page_set_bool(g_page, "dark", app().darkTheme ? 1 : 0);
        app().needsRefresh = true;
    }
    if (lc != g_lastLangCmd) {
        g_lastLangCmd = lc;
        int l = 0;
        if (char* j = ui_page_get_json(g_page, "lang")) { l = jsonInt(j, 0); ui_page_free(j); }
        if (l < 0 || l > 1) l = 0;
        app().lang = (uint8_t)l;
        app().dirty = true;   // 语言偏好随下次 TI_SAVE 落盘
        if (g_win) ui_window_set_title(g_win, app().lang == 0 ? KMT_APP_TITLE : L"KeyMouseTracker");
        app().needsRefresh = true;
    }
    if (xc != g_lastExportCmd) {
        g_lastExportCmd = xc;
        if (!app().days.empty()) doExportBackup();
        else msgInfo(tr(L"暂无数据可导出。", L"No data to export."));
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
    // 明细时间范围：自定义范围确认后读取 detailStart/detailEnd 聚合
    if (char* j = ui_page_get_json(g_page, "detailRangeCmd")) {
        int rc = jsonInt(j, 0);
        ui_page_free(j);
        if (rc != g_lastDetailRangeCmd) {
            g_lastDetailRangeCmd = rc;
            std::string s1, s2;
            if (char* nj = ui_page_get_json(g_page, "detailStart")) { s1 = jsonText(nj, ""); ui_page_free(nj); }
            if (char* nj = ui_page_get_json(g_page, "detailEnd"))   { s2 = jsonText(nj, ""); ui_page_free(nj); }
            applyDetailRange(s1, s2);
            app().needsRefresh = true;
            if (g_win) ui_window_invalidate(g_win);
        }
    }
    if (char* j = ui_page_get_json(g_page, "detailRangeCancelCmd")) {
        int ccc = jsonInt(j, 0);
        ui_page_free(j);
        if (ccc != g_lastDetailRangeCancelCmd) {
            g_lastDetailRangeCancelCmd = ccc;
            if (!g_detailAllTime) {
                g_detailAllTime = true;
                g_detailDirty = true;
                app().needsRefresh = true;
                if (g_win) ui_window_invalidate(g_win);
            }
        }
    }
    // 按应用筛选：filterAppCmd 增量触发，读取 filterApp 为目标应用名
    if (char* j = ui_page_get_json(g_page, "filterAppCmd")) {
        int fc = jsonInt(j, 0);
        ui_page_free(j);
        if (fc != g_lastFilterCmd) {
            g_lastFilterCmd = fc;
            std::string name;
            if (char* nj = ui_page_get_json(g_page, "filterApp")) {
                name = jsonText(nj, ""); ui_page_free(nj);
            }
            if (name != g_filterApp) {
                g_filterApp = name;
                g_filterCacheDirty = true;   // 聚合缓存整体重建
                g_detailDirty = true;        // 明细聚合数据源（appMin/g_filterApp）已变
                g_trHover = -1;
                g_trendChart.hoverBucket = -1; g_trendChart.hoverActive = false;
                g_trendChart.clearCaches();   // 时间序列桶数据源已变（应用过滤），缓存作废
                g_chartWarmup = 8;
                app().needsRefresh = true;
                if (g_win) ui_window_invalidate(g_win);
            }
        }
    }
    // 悬停/点选应用：hoverAppCmd / pinnedAppCmd 增量触发；右侧 24h 趋势按 固定>悬停 过滤
    {
        bool changed = false;
        if (char* j = ui_page_get_json(g_page, "hoverAppCmd")) {
            int hc = jsonInt(j, 0); ui_page_free(j);
            if (hc != g_lastHoverAppCmd) {
                g_lastHoverAppCmd = hc;
                std::string name;
                if (char* nj = ui_page_get_json(g_page, "hoverApp")) { name = jsonText(nj, ""); ui_page_free(nj); }
                if (name != g_hoverApp) { g_hoverApp = name; changed = true; }
            }
        }
        if (char* j = ui_page_get_json(g_page, "pinnedAppCmd")) {
            int pc = jsonInt(j, 0); ui_page_free(j);
            if (pc != g_lastPinnedAppCmd) {
                g_lastPinnedAppCmd = pc;
                std::string name;
                if (char* nj = ui_page_get_json(g_page, "pinnedApp")) { name = jsonText(nj, ""); ui_page_free(nj); }
                if (name != g_pinnedApp) { g_pinnedApp = name; changed = true; }
            }
        }
        if (changed) {
            g_apmChart.invalidateData();   // 数据源变化，失效 24h 趋势缓存
            g_chartWarmup = 8;             // 预热帧：连续重绘数帧消除首帧冷启动卡顿
            if (g_win) ui_window_invalidate(g_win);
        }
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

    // 趋势图模式切换（0=统计表 1=明细[P4] 2=时段）
    if (char* j = ui_page_get_json(g_page, "trendModeIdx")) {
        int m = jsonInt(j, g_trendMode);
        if (m < 0 || m > 2) m = g_trendMode;
        if (m != g_trendMode) {
            g_trendMode = m;
            g_trHover = -1;             // 清除旧模式悬停残留
            g_trendChart.hoverBucket = -1; g_trendChart.hoverActive = false;
            g_trendChart.dragging = false;
            g_detailDirty = true;       // 进入明细模式需按当前范围重算
            app().needsRefresh = true;
            if (g_win) ui_window_invalidate(g_win);   // 模式切换立即重绘（否则要等下次数据更新）
        }
        ui_page_free(j);
    }
    // 时段 scope（0=日 1=周 2=月）与系列单选（0=按键 1=点击 2=里程 3=活跃）
    if (char* j = ui_page_get_json(g_page, "heatScope")) {
        int s = jsonInt(j, g_heatScope);
        if (s >= 0 && s <= 2 && s != g_heatScope) {
            g_heatScope = s;
            g_trHover = -1;
            app().needsRefresh = true;
            if (g_win) ui_window_invalidate(g_win);
        }
        ui_page_free(j);
    }
    if (char* j = ui_page_get_json(g_page, "heatSeries")) {
        int s = jsonInt(j, g_heatSeries);
        if (s >= 0 && s <= 3 && s != g_heatSeries) {
            g_heatSeries = s;
            g_trHover = -1;
            app().needsRefresh = true;
            if (g_win) ui_window_invalidate(g_win);
        }
        ui_page_free(j);
    }
    // 多系列勾选（T7 前置）：trendSeries=[按键,点击,里程,活跃] 0/1 → g_trendChart.enabled（保留系列下标）。
    // 不置 g_trendChartDirty：enabled 由 ensureData 在每次绘制时从缓存过滤应用，只重绘即可，避免重置缩放/平移视口。
    if (char* j = ui_page_get_json(g_page, "trendSeries")) {
        std::vector<int> idx = parseSeriesFlags(j);
        // 去掉 !idx.empty() 限制，允许"全不选"（用户取消最后一个系列时图表清空）。
        // 之前 JS 层 toggleSeries 已拦截至少保留一个，C++ 侧的 !idx.empty() 保护与 UI 层
        // 双重约束使图表绘制逻辑与 UI 状态机冲突（导致卡顿/延迟取消/切换无效等问题）。
        // 现改为单点判定 idx != g_trendChart.enabled，UI 状态即真值，图表严格跟随勾选。
        if (idx != g_trendChart.enabled) {
            g_trendChart.enabled = idx;
            g_trendChart.setAlphaTargetToEnabled();   // 立即更新透明度目标，让淡入淡出正确启动
            g_trendChart.animating = true;            // 60fps 驱动淡入淡出 + 纵轴平滑重定标
            g_trendChart.hoverBucket = -1; g_trendChart.hoverActive = false;
            g_chartWarmup = 8;                        // 预热帧：系列切换仅改图表，无需全量 pushStats
            if (g_win) ui_window_invalidate(g_win);
        }
        ui_page_free(j);
    }
    // 总览叠加系列（T7/P5）：overviewSeries=[按键,点击,里程,活跃] 0/1 → g_overviewSeries（下标列表）。
    // 全不勾合法：总览严格按勾选显示（无 APM 恒显），空=仅空白（D7 bug 修复）。
    // 勾选 bug 修复：立即映射到 g_apmChart.enabled 并强制重绘；下标 0..3 直接对应
    // 按键/点击/里程/活跃，取消勾选即从列表移除、ensureData 过滤。
    if (char* j = ui_page_get_json(g_page, "overviewSeries")) {
        std::vector<int> idx = parseSeriesFlags(j);
        if (idx != g_overviewSeries) {
            g_overviewSeries = idx;
            if (idx != g_apmChart.enabled) {
                g_apmChart.enabled = idx;
                g_apmChart.setAlphaTargetToEnabled();
                g_apmChart.animating = true;
                g_apmChart.hoverBucket = -1; g_apmChart.hoverActive = false;
            }
            g_chartWarmup = 8;                        // 预热帧：系列切换仅改图表，无需全量 pushStats
            if (g_win) ui_window_invalidate(g_win);
        }
        ui_page_free(j);
    }
    // 图表类型切换：trendChartType "bar"=柱状 "line"=折线 → g_trendChart.line
    if (char* j = ui_page_get_json(g_page, "trendChartType")) {
        bool wantLine = jsonText(j, "bar") == "line";
        if (wantLine != g_trendChart.line) {
            g_trendChart.line = wantLine;
            g_trendChart.hoverBucket = -1; g_trendChart.hoverActive = false;
            g_chartWarmup = 8;                        // 预热帧：柱/折线切换消除首帧冷启动卡顿
            if (g_win) ui_window_invalidate(g_win);
        }
        ui_page_free(j);
    }
    // 键盘配列切换
    if (char* j = ui_page_get_json(g_page, "kbLayout")) {
        int l = jsonInt(j, (int)app().kbLayout);
        if (l < 0 || l > 2) l = app().kbLayout;
        if (l != (int)app().kbLayout) {
            app().kbLayout = (uint8_t)l;
            app().dirty = true;
            app().needsRefresh = true;
            // 立即重绘热力图（配列只影响绘制布局，不改变任何统计键，
            // pushStats 的"内容变化才 invalidate"不会触发，必须直接请求重绘）
            if (g_win) ui_window_invalidate(g_win);
        }
        ui_page_free(j);
    }
    // 日期选择（统计表读 dateStr；时段按 scope 读 dateStr/weekStr/monthStr）
    bool dateDirty = false;
    if (g_trendMode == 0) {
        if (char* j = ui_page_get_json(g_page, "dateStr")) {
            std::string s = jsonText(j, "");
            int y, m, d;
            if (parseYMDLoose(s, 8, y, m, d) &&
                (y != g_statSelY || m != g_statSelM || d != g_statSelD)) {
                g_statSelY = y; g_statSelM = m; g_statSelD = d;
                dateDirty = true;
            }
            ui_page_free(j);
        }
    } else if (g_trendMode == 2) {
        if (g_heatScope == 2) {   // 时段-月：monthStr
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
        } else {   // 时段-日/周：dateStr / weekStr（周一）
            const char* key = (g_heatScope == 1) ? "weekStr" : "dateStr";
            if (char* j = ui_page_get_json(g_page, key)) {
                std::string s = jsonText(j, "");
                int y, m, d;
                if (parseYMDLoose(s, 8, y, m, d) &&
                    (y != g_trendSelY || m != g_trendSelM || d != g_trendSelD)) {
                    g_trendSelY = y; g_trendSelM = m; g_trendSelD = d;
                    dateDirty = true;
                }
                ui_page_free(j);
            }
        }
    }
    if (dateDirty) {
        if (g_trendMode == 0) g_trendChartDirty = true;   // 仅统计模式日期变化时重建视口
        g_trendChart.hoverBucket = -1; g_trendChart.hoverActive = false;
        app().needsRefresh = true;
        if (g_win) ui_window_invalidate(g_win);   // 日期切换立即重绘
    }
}

static VOID CALLBACK TimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    switch (id) {
    case TI_SAMPLE: {
        drainInputQueue();   // 排空钩子入队的按键/点击事件（回调脱敏，统计在此计入）
        SampleMouseMove();   // 光标位移里程采样（GetCursorPos）
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
            // 持续无活动达到空闲阈值（idleMin 分钟）后计入空闲秒——
            // idleSec 此前只落盘/展示、从不累计（恒为 0），idleMin 配置也从未生效，此处一并生效。
            if (since >= (DWORD)app().idleMin * 60000) {
                t.idleSec++;
                app().dirty = true;
            }
        }
        break;
    }
    case TI_POLL: {
        ApiServe();                         // 处理命名管道 API 的挂起请求（主线程取数）
        pollCommands();
        if (!g_foreHook) pollForeApp();     // 事件钩子未安装时回退到 500ms 轮询
        if (g_trayRetry > 0) {              // 启动初期兜底重试：Explorer 晚就绪时补加托盘图标
            --g_trayRetry;
            TrayAddIcon();
            TrayUpdate(app().paused);
        }
        break;
    }
    case TI_REFRESH: {                      // 60 帧 UI 刷新轮询
        if (g_win && !g_inResizeMode && IsWindowVisible(g_hwnd)) {
            // 每帧无条件轮询 JS → C++ 命令。旧实现仅当图表动画进行时才
            // pollCommands，导致点击普通按钮（Toggle/Expander/Tab 等 core-ui 内置
            // 动画控件）后，用户点击产生的 cmd 增量最多等到 500ms TI_POLL 才被读
            // 取，视觉表现是"点击后卡在按下前状态直到下次全局刷新"。这里改为 16ms
            // 逐帧同步，pollCommands 内部通过 g_lastXxxCmd 差分幂等，无副作用。
            pollCommands();
            bool inv = false;
            // 动画 tick 由多媒体定时器（MMTimerProc）驱动，此处不再每帧 tick。
            // 数据切换后的预热帧：连续重绘数帧，把渲染管线从空闲状态拉起。
            if (g_chartWarmup > 0) { --g_chartWarmup; inv = true; }
            if (inv) ui_window_invalidate(g_win);
        }
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
        // 自适应频率：动画/预热/待刷新时 16ms，纯空闲时 100ms（减少空闲唤醒 84%）
        {
            UINT want = 100;
            if (g_chartWarmup > 0 || g_trendChart.animating || g_apmChart.animating || app().needsRefresh)
                want = kRefreshMs;
            if (want != g_curRefreshMs) {
                g_curRefreshMs = want;
                SetTimer(g_hwnd, TI_REFRESH, want, TimerProc);
            }
        }
        break;
    }
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmd, int) {
    timeBeginPeriod(1);   // 提升定时器分辨率，保证 16ms 动画帧稳定
    // 单实例互斥
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\KeyMouseTracker.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(nullptr, KMT_APP_TITLE);
        if (!w) w = FindWindowW(nullptr, L"KeyMouseTracker");
        if (w) { ShowWindow(w, SW_SHOW); ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
        CloseHandle(mutex);
        return 0;
    }

    // 加载数据
    if (!loadData(dataFilePath())) {
        // 数据加载失败：检查现有文件是否显著大于预期空数据——
        // 若是，说明旧数据可能有价值，重命名保留而非覆盖
        std::wstring df = dataFilePath();
        DWORD attrs = GetFileAttributesW(df.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            HANDLE hf = CreateFileW(df.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hf != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER fs;
                GetFileSizeEx(hf, &fs);
                CloseHandle(hf);
                if (fs.QuadPart > 4096) {
                    std::wstring orphan = df + L".orphan";
                    MoveFileExW(df.c_str(), orphan.c_str(), MOVEFILE_REPLACE_EXISTING);
                    // 同时保留 .bak
                    std::wstring bak = df + L".bak";
                    if (GetFileAttributesW(bak.c_str()) != INVALID_FILE_ATTRIBUTES)
                        MoveFileExW(bak.c_str(), (df + L".bak.orphan").c_str(), MOVEFILE_REPLACE_EXISTING);
                }
            }
        }
    }
    // 前台应用统计强制开启，UI 无关闭按钮。
    // 旧数据文件可能仍存 optAppTrack=false，加载后强制翻正并标记 dirty，
    // 由下一次 TI_SAVE 落盘。pollForeApp 从首帧开始即采集前台进程归因。
    if (!app().optAppTrack) { app().optAppTrack = true; app().dirty = true; }
    ensureCurDay();

    // 高精度动画定时器：tick 走多媒体定时器（独立线程，不受 WM_TIMER 消息循环抖动影响）
    InitializeCriticalSection(&g_chartLock);
    g_mmTimerId = timeSetEvent(16, 1, MMTimerProc, 0, TIME_PERIODIC);

    // 初始化 UI
    ui_init_with_theme(app().darkTheme ? UI_THEME_DARK : UI_THEME_LIGHT);
    g_page = ui_page_load_string(k_app_uix);
    if (!g_page) {
        MessageBoxW(nullptr, tr(L"UI 页面加载失败（ui_page_load_string 返回空）。", L"Failed to load UI page (ui_page_load_string returned null)."), KMT_APP_TITLE, MB_OK | MB_ICONERROR);
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
        MessageBoxW(nullptr, wErr.c_str(), tr(L"键鼠使用记录 - UI 初始化失败", L"KeyMouseTracker - UI init failed"), MB_OK | MB_ICONERROR);
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
    // 版本号（单一来源 version.h）与语言偏好注入（uix 侧响应式渲染）
    {
        std::string v = std::string("\"") + KMT_APP_VERSION + "\"";
        ui_page_set_json(g_page, "version", v.c_str());
        ui_page_set_json(g_page, "lang", app().lang == 0 ? "0" : "1");
    }
    ui_window_set_title(g_win, app().lang == 0 ? KMT_APP_TITLE : L"KeyMouseTracker");

    SetWindowSubclass(g_hwnd, SubclassProc, kSubclassId, 0);

    // 注册 TaskbarCreated：托盘被重建（含开机自启时 Explorer 晚起）时重加图标
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // 安装全局钩子
    if (!InstallHooks()) {
        msgInfo(tr(L"无法安装全局钩子，请以普通进程身份运行。", L"Failed to install global hooks; please run as an elevated process."), UI_MSGBOX_ICON_WARNING);
        ui_page_destroy(g_page);
        ui_shutdown();
        ReleaseMutex(mutex);
        return 3;
    }

    InstallForeHook();   // 前台应用事件驱动归因（安装失败则回退 500ms 轮询）
    pollForeApp();       // 启动时初始探测当前前台应用

    TrayInit(g_win, g_hwnd);   // 托盘模块记录窗口句柄（还原/弹窗用）
    TrayAddIcon();
    TrayUpdate(false);
    g_trayRetry = 20;   // 启动后 10s 内每 500ms 重试一次托盘图标，兜底 Explorer 晚就绪

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

    ApiStart();   // 启动命名管道 JSON API（后台线程，失败不影响主程序）

    int code = ui_run();

    if (g_mmTimerId) { timeKillEvent(g_mmTimerId); g_mmTimerId = 0; }
    DeleteCriticalSection(&g_chartLock);
    KillTimer(g_hwnd, TI_SAMPLE);
    KillTimer(g_hwnd, TI_SAVE);
    KillTimer(g_hwnd, TI_ACTIVE);
    KillTimer(g_hwnd, TI_POLL);
    KillTimer(g_hwnd, TI_REFRESH);
    RemoveWindowSubclass(g_hwnd, SubclassProc, kSubclassId);

    UninstallHooks();
    if (g_foreHook) { UnhookWinEvent(g_foreHook); g_foreHook = nullptr; }
    saveData(dataFilePath());
    ApiStop();
    TrayCleanup();

    ui_page_destroy(g_page);
    ui_shutdown();
    ReleaseMutex(mutex);
    return code;
}
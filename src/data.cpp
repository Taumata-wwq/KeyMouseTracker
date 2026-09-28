// 数据模型与持久化
#include "data.h"
#include "version.h"
#include <shlobj.h>
#include <cstdio>
#include <algorithm>

const wchar_t* kAppName = KMT_APP_NAME;

static AppData g_app;
AppData& app() { return g_app; }

static std::map<uint8_t, uint32_t> g_cumKeys;
static std::map<uint32_t, uint32_t> g_cumHeat;

// 主数据文件字节数缓存：避免每次 UI 刷新都做一次磁盘 stat()。
// 仅在写盘（saveData 写入主数据文件）成功后精确更新，其余时候直接读缓存。
static uint64_t g_cachedBytes = 0;
static bool    g_cachedBytesValid = false;

const std::map<uint8_t, uint32_t>& cumulativeKeys() { return g_cumKeys; }
const std::map<uint32_t, uint32_t>& cumulativeHeat() { return g_cumHeat; }

static int days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static const int kBase = days_from_civil(2020, 1, 1);

static int   g_cachedDay = -1;
static int   g_cachedHour = -1;
static int   g_cachedMin = -1;
static DWORD g_cachedTick = 0;

static void refreshClock() {
    DWORD now = GetTickCount();
    if (g_cachedDay >= 0 && (now - g_cachedTick) < 500) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    g_cachedDay = days_from_civil((int)st.wYear, st.wMonth, st.wDay) - kBase;
    g_cachedHour = (int)st.wHour;
    g_cachedMin = (int)st.wHour * 60 + (int)st.wMinute;
    g_cachedTick = now;
}

int dayIndexFromYMD(int y, int m, int d) {
    return days_from_civil(y, (unsigned)m, (unsigned)d) - kBase;
}

int daysInMonth(int y, int m) {
    if (m < 1) m = 1; else if (m > 12) m = 12;
    int ny = y, nm = m + 1;
    if (nm > 12) { nm = 1; ny = y + 1; }
    return days_from_civil(ny, (unsigned)nm, 1) - days_from_civil(y, (unsigned)m, 1);
}

std::string dayIndexToStr(int idx) {
    int d = kBase + idx;
    int z = d + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d2 = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d2);
    return buf;
}

std::wstring dataFilePath() {
    wchar_t buf[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf) == S_OK) {
        std::wstring dir = std::wstring(buf) + L"\\" + kAppName;
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\data.bin";
    }
    return std::wstring(L"data.bin");
}

void ensureCurDay() {
    refreshClock();
    int cur = g_cachedDay;
    if (app().days.empty() || app().days.rbegin()->first != (uint16_t)cur) {
        DayData dd;
        dd.day = (uint16_t)cur;
        app().days[cur] = dd;
    }
    app().cur = (uint16_t)cur;
    app().days[cur].day = (uint16_t)cur;
}

static void touch() {
    app().lastActivity = GetTickCount();
    app().dirty = true;
    app().needsRefresh = true;
}

static std::string g_foreApp;   // 当前前台应用（exe 名，UTF-8），由 main 定时轮询刷新
const std::string& currentForeApp() { return g_foreApp; }
void setCurrentForeApp(const std::string& name) { g_foreApp = name; }

// 当前可归因的前台应用名（optAppTrack 开启且前台有效且不在排除列表），否则返回空串
static const std::string& attrTarget() {
    static const std::string empty;
    if (!app().optAppTrack) return empty;
    if (g_foreApp.empty()) return empty;
    if (app().excludeApps.count(g_foreApp)) return empty;
    return g_foreApp;
}

// 前台应用归因（仅 optAppTrack 开启）：按键/点击计入当前前台进程（排除列表内不计）
static void attrApp(DayData& t) {
    const std::string& a = attrTarget();
    if (a.empty()) return;
    t.appCounts[a]++;
}

// —— 输入事件队列（钩子回调脱敏）——
// 低层钩子（WH_KEYBOARD_LL / WH_MOUSE_LL）由安装线程的消息循环回调，与主线程计时器
// 串行执行，故此处用无锁 vector 即可。回调只做 O(1) push（保留容量避免反复分配），
// 其余统计写入由 drainInputQueue() 在计时器中完成，保证回调永不触碰 map/IO。
struct PendingInput { uint8_t kind; uint8_t a; int32_t x; int32_t y; };
static std::vector<PendingInput> g_pending;
static void enqueueInput(uint8_t kind, uint8_t a, int32_t x, int32_t y) {
    if (g_pending.size() >= 65536) return;   // 硬上限兜底（人类输入不会触顶）
    g_pending.push_back({ kind, a, x, y });
}
void enqueueKey(uint8_t vk) { enqueueInput(0, vk, 0, 0); }
void enqueueClick(uint8_t btn, LONG x, LONG y) { enqueueInput(1, btn, (int32_t)x, (int32_t)y); }
void drainInputQueue() {
    if (g_pending.empty()) return;
    for (const PendingInput& e : g_pending) {
        if (e.kind == 0) recordKey(e.a);
        else recordClick(e.a, e.x, e.y);
    }
    g_pending.clear();   // 保留容量，避免每轮重分配
}

void recordKey(uint8_t vk) {
    if (app().paused) return;
    refreshClock();
    ensureCurDay();
    DayData& t = app().days[app().cur];
    t.keyCounts[vk]++;
    t.keys++;
    attrApp(t);
    if (g_cachedHour >= 0 && g_cachedHour < 24) {
        t.hourlyKeys[g_cachedHour]++;
        t.keyHourly[(uint32_t)g_cachedHour * 256 + vk]++;   // 每时每键明细
    }
    if (g_cachedMin >= 0) { t.minuteActivity[g_cachedMin]++; t.keyMinuteActivity[g_cachedMin]++; }  // 分钟强度（键）
    if (g_cachedMin >= 0) {   // 应用 × 分钟细化（仅 optAppTrack 开启且有前台应用）
        const std::string& a = attrTarget();
        if (!a.empty()) t.appMin[a].keyByMinute[g_cachedMin][vk]++;
    }
    g_cumKeys[vk]++;   // 增量维护累计缓存
    touch();
}

// 屏幕绝对坐标 -> 热力网格索引
int heatIndexFromScreen(LONG x, LONG y) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return -1;
    long gx = (long)((double)(x - vx) * kHeatW / vw);
    long gy = (long)((double)(y - vy) * kHeatH / vh);
    if (gx < 0) gx = 0; else if (gx >= kHeatW) gx = kHeatW - 1;
    if (gy < 0) gy = 0; else if (gy >= kHeatH) gy = kHeatH - 1;
    return (int)(gy * kHeatW + gx);
}

const char* vkLabel(uint8_t vk, char buf[32]) {
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
        case VK_NUMLOCK: return "Num";
        case VK_MULTIPLY: return "N*"; case VK_ADD: return "N+";
        case VK_SUBTRACT: return "N-"; case VK_DECIMAL: return "N."; case VK_DIVIDE: return "N/";
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

// JSON 字符串转义（UTF-8 字节流）：<0x20 的控制字符 → \uXXXX，引号/反斜杠转义
std::string jsonEscape(const char* s) {
    std::string r;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        unsigned char c = *p;
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\b': r += "\\b"; break;
            case '\f': r += "\\f"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); r += b; }
                else r += (char)c;
        }
    }
    return r;
}

void recordClick(uint8_t btn, LONG x, LONG y) {
    if (app().paused) return;
    refreshClock();
    ensureCurDay();
    DayData& t = app().days[app().cur];
    t.clicks++;
    if (btn == 1) t.mLeft++;
    else if (btn == 2) t.mRight++;
    else if (btn == 3) t.mMid++;
    attrApp(t);
    int idx = heatIndexFromScreen(x, y);
    if (idx >= 0) { t.mouseHeat[idx]++; g_cumHeat[idx]++; }
    if (g_cachedHour >= 0 && g_cachedHour < 24) t.hourlyClicks[g_cachedHour]++;
    if (g_cachedMin >= 0) { t.minuteActivity[g_cachedMin]++; t.clickMinuteActivity[g_cachedMin]++; }  // 分钟强度（点击）
    if (g_cachedMin >= 0) {   // 应用 × 分钟细化（仅 optAppTrack 开启且有前台应用）
        const std::string& a = attrTarget();
        if (!a.empty()) {
            AppMinuteData& am = t.appMin[a];
            am.clickBtnMinute[g_cachedMin]++;
            if (btn == 1) am.leftBtnMinute[g_cachedMin]++;
            else if (btn == 2) am.rightBtnMinute[g_cachedMin]++;
            else if (btn == 3) am.midBtnMinute[g_cachedMin]++;
            if (idx >= 0) am.clickByMinute[g_cachedMin][(uint32_t)idx]++;
        }
    }
    touch();
}

void recordMove() {
    if (app().paused) return;
    refreshClock();
    ensureCurDay();
    DayData& t = app().days[app().cur];
    t.motion++;
    if (g_cachedMin >= 0) {   // 应用 × 分钟移动采样（仅 optAppTrack 开启且有前台应用）
        const std::string& a = attrTarget();
        if (!a.empty()) t.appMin[a].motionByMinute[g_cachedMin]++;
    }
    // 每次采样都请求 UI 刷新：移动数据即时更新；推送成本已由 main 侧差分
    // 更新（pushStats 仅推送变化字段）消化，无需再降频。
    app().lastActivity = GetTickCount();
    app().needsRefresh = true;
}

void recordMoveDist(uint64_t px) {
    if (app().paused) return;
    refreshClock();
    ensureCurDay();
    DayData& t = app().days[app().cur];
    t.distPx += px;
    if (g_cachedMin >= 0) {   // 应用 × 分钟移动像素（仅 optAppTrack 开启且有前台应用）
        const std::string& a = attrTarget();
        if (!a.empty()) t.appMin[a].movePxByMinute[g_cachedMin] += (uint32_t)px;
    }
    // 不单独置 dirty：调用方（TI_SAMPLE 移动路径）通常已先 recordMove() 置过
}

uint64_t distToCm(uint64_t px) {
    static double dpi = 0;
    if (dpi <= 0) {
        HDC dc = GetDC(nullptr);
        dpi = (double)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        if (dpi <= 0) dpi = 96.0;
    }
    return (uint64_t)((double)px * 2.54 / dpi + 0.5);
}

// —— 应用 × 分钟聚合助手（供 UI 下钻；exe 空串 = 全部应用；区间闭上闭下） ——
static bool inMinRange(int min, int minStart, int minEnd) {
    return (minStart < 0 || min >= minStart) && (minEnd < 0 || min <= minEnd);
}

static void gatherAppKeys(const AppMinuteData& am, int ma, int mb, uint64_t& out) {
    for (auto& mm : am.keyByMinute)
        if (inMinRange((int)mm.first, ma, mb))
            for (auto& kv : mm.second) out += kv.second;
}

uint64_t appKeys(const DayData& d, const std::string& exe, int minStart, int minEnd) {
    uint64_t total = 0;
    if (!exe.empty()) {
        auto it = d.appMin.find(exe);
        if (it != d.appMin.end()) gatherAppKeys(it->second, minStart, minEnd, total);
        return total;
    }
    for (auto& ap : d.appMin) gatherAppKeys(ap.second, minStart, minEnd, total);
    return total;
}

static void gatherAppClicks(const AppMinuteData& am, int ma, int mb, uint64_t& out) {
    for (auto& mm : am.clickByMinute)
        if (inMinRange((int)mm.first, ma, mb))
            for (auto& g : mm.second) out += g.second;
}

uint64_t appClicks(const DayData& d, const std::string& exe, int minStart, int minEnd) {
    uint64_t total = 0;
    if (!exe.empty()) {
        auto it = d.appMin.find(exe);
        if (it != d.appMin.end()) gatherAppClicks(it->second, minStart, minEnd, total);
        return total;
    }
    for (auto& ap : d.appMin) gatherAppClicks(ap.second, minStart, minEnd, total);
    return total;
}

static void gatherAppMotionPx(const AppMinuteData& am, int ma, int mb, uint64_t& out) {
    for (auto& mm : am.movePxByMinute)
        if (inMinRange((int)mm.first, ma, mb)) out += mm.second;
}

uint64_t appMotionPx(const DayData& d, const std::string& exe, int minStart, int minEnd) {
    uint64_t total = 0;
    if (!exe.empty()) {
        auto it = d.appMin.find(exe);
        if (it != d.appMin.end()) gatherAppMotionPx(it->second, minStart, minEnd, total);
        return total;
    }
    for (auto& ap : d.appMin) gatherAppMotionPx(ap.second, minStart, minEnd, total);
    return total;
}

// 活跃分钟数：任一子 map (key/click/motion/movePx/clickBtn) 中出现即视为该分钟应用活跃。
// 用于 24h 综合分：把"持续在前景时间"作为独立维度，与键/点击/里程互补。
// 五个子 map 的 value 类型混合（uint16_t / uint32_t / 嵌套 map），按键分钟插入集合去重。
uint64_t appActiveMin(const DayData& d, const std::string& exe, int minStart, int minEnd) {
    auto addKeysOf = [](const auto& m, int ma, int mb, std::set<uint16_t>& mins) {
        for (auto& kv : m) if (inMinRange((int)kv.first, ma, mb)) mins.insert(kv.first);
    };
    auto gather = [&](const AppMinuteData& am, std::set<uint16_t>& mins) {
        addKeysOf(am.keyByMinute,     minStart, minEnd, mins);   // min -> { vk: c }
        addKeysOf(am.clickByMinute,   minStart, minEnd, mins);   // min -> { heat: c }
        addKeysOf(am.motionByMinute,  minStart, minEnd, mins);   // min -> c (uint16)
        addKeysOf(am.movePxByMinute,  minStart, minEnd, mins);   // min -> px (uint32)
        addKeysOf(am.clickBtnMinute,  minStart, minEnd, mins);   // min -> c (uint16)
    };
    std::set<uint16_t> mins;
    if (!exe.empty()) {
        auto it = d.appMin.find(exe);
        if (it != d.appMin.end()) gather(it->second, mins);
        return (uint64_t)mins.size();
    }
    for (auto& ap : d.appMin) gather(ap.second, mins);
    return (uint64_t)mins.size();
}

// 前置声明（eraseRange 使用）
static void rebuildCumulative();
// 前置声明（saveData 使用）
static void CompressLZSS(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out);
static bool DecompressLZSS(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out);

// 按日期索引区间删除（含边界）。调用方需在删除后 ensureCurDay() 重建“今天”条目。
void eraseRange(int startIdx, int endIdx) {
    auto it = app().days.lower_bound((uint16_t)startIdx);
    while (it != app().days.end() && (int)it->first <= endIdx) it = app().days.erase(it);
    rebuildCumulative();
    app().dirty = true;
    app().needsRefresh = true;
}

StorageInfo storageInfo() {
    StorageInfo si;
    // 字节数优先读缓存（本轮运行写盘后就是精确值），仅首次/未写盘时做一次磁盘 stat
    if (g_cachedBytesValid) {
        si.bytes = g_cachedBytes;
    } else {
        std::wstring path = dataFilePath();
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz;
            if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 0) si.bytes = (uint64_t)sz.QuadPart;
            CloseHandle(h);
        }
        g_cachedBytes = si.bytes;
        g_cachedBytesValid = true;
    }
    si.days = (int)app().days.size();
    if (!app().days.empty()) {
        si.first = app().days.begin()->first;
        si.last = app().days.rbegin()->first;
    }
    return si;
}

static void writeU16(std::vector<unsigned char>& out, uint16_t v) {
    out.push_back((unsigned char)(v & 0xFF));
    out.push_back((unsigned char)((v >> 8) & 0xFF));
}
static void writeU32(std::vector<unsigned char>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}

// LEB128 无符号可变长整数：小数值（键/格计数、时间等）1–2 字节，显著压缩数据文件
static void writeVarint(std::vector<unsigned char>& out, uint64_t v) {
    while (v >= 0x80) { out.push_back((unsigned char)((v & 0x7F) | 0x80)); v >>= 7; }
    out.push_back((unsigned char)v);
}
static uint64_t readVarint(const unsigned char*& p, const unsigned char* end) {
    uint64_t v = 0; int shift = 0;
    while (p < end) {
        unsigned char b = *p++;
        v |= ((uint64_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift >= 64) break;
    }
    return v;
}

// KMvarint 编码版本（v12 布局，读取仅支持当前版本）：
// "KMT5" u32 version
// u16 dayCount
// 每 Day：u16 day
// varint keys clicks mLeft mMid mRight activeSec motion distPx
// varint idleSec maxSessionSec sessionCount          (v6+)
// varint minuteActivity.size; (varint minute, varint count)*   (v6+)
// varint keyMinuteActivity.size; (varint minute, varint count)*   (v9+)
// varint clickMinuteActivity.size; (varint minute, varint count)*   (v9+)
// varint keyHourly.size;      (varint key, varint count)*     (v6+)
// varint keyCounts.size; (varint vk, varint count)*
// varint mouseHeat.size;  (varint idx, varint count)*
// varint hourlyKeys[24];  varint hourlyClicks[24]
// varint appCounts.size; (varint namelen, utf8 bytes, varint count)*   (v7+)
// varint appMin.size; { appEntry }*                                     (v11+)
// appEntry: varint namelen, utf8 bytes,                              (与 appCounts 同名应用)
// varint keyByMinute.size; (varint min, varint vkSize, (varint vk, varint count)*)*
// varint clickByMinute.size; (varint min, varint gridSize, (varint idx, varint count)*)*
// varint motionByMinute.size; (varint min, varint count)*
// varint movePxByMinute.size; (varint min, varint px)*
// varint clickBtnMinute.size; (varint min, varint count)*
// varint leftBtnMinute.size; (varint min, varint count)*      (v12+)
// varint midBtnMinute.size; (varint min, varint count)*       (v12+)
// varint rightBtnMinute.size; (varint min, varint count)*     (v12+)
// 全局：u8 darkTheme
// varint hiddenKeys.size; varint vk*
// u8 kbLayout
// u8 optAppTrack     (v7+)
// varint excludeApps.size + (varint namelen, utf8 bytes)* ; u8 idleMin   (v8+)
bool saveData(const std::wstring& path) {
    std::vector<unsigned char> out;
    out.insert(out.end(), { 'K', 'M', 'T', '5' });
    writeU32(out, KMT_DATA_VERSION); // version
    auto& days = app().days;
    writeU16(out, (uint16_t)days.size());
    for (auto it = days.begin(); it != days.end(); ++it) {
        const DayData& d = it->second;
        writeU16(out, d.day);
        writeVarint(out, d.keys);
        writeVarint(out, d.clicks);
        writeVarint(out, d.mLeft);
        writeVarint(out, d.mMid);
        writeVarint(out, d.mRight);
        writeVarint(out, d.activeSec);
        writeVarint(out, d.motion);
        writeVarint(out, d.distPx);
        writeVarint(out, d.idleSec);
        writeVarint(out, d.maxSessionSec);
        writeVarint(out, d.sessionCount);
        writeVarint(out, d.minuteActivity.size());
        for (auto& kv : d.minuteActivity) { writeVarint(out, kv.first); writeVarint(out, kv.second); }
        // v9：分钟级按键/点击分离
        writeVarint(out, d.keyMinuteActivity.size());
        for (auto& kv : d.keyMinuteActivity) { writeVarint(out, kv.first); writeVarint(out, kv.second); }
        writeVarint(out, d.clickMinuteActivity.size());
        for (auto& kv : d.clickMinuteActivity) { writeVarint(out, kv.first); writeVarint(out, kv.second); }
        writeVarint(out, d.keyHourly.size());
        for (auto& kv : d.keyHourly) { writeVarint(out, kv.first); writeVarint(out, kv.second); }
        writeVarint(out, d.keyCounts.size());
        for (auto& kv : d.keyCounts) { writeVarint(out, kv.first); writeVarint(out, kv.second); }
        writeVarint(out, d.mouseHeat.size());
        for (auto& kv : d.mouseHeat) { writeVarint(out, kv.first); writeVarint(out, kv.second); }
        for (int i = 0; i < 24; ++i) writeVarint(out, d.hourlyKeys[i]);
        for (int i = 0; i < 24; ++i) writeVarint(out, d.hourlyClicks[i]);
        writeVarint(out, d.appCounts.size());
        for (auto& ac : d.appCounts) {
            writeVarint(out, (uint64_t)ac.first.size());
            out.insert(out.end(), ac.first.begin(), ac.first.end());
            writeVarint(out, ac.second);
        }
        // v11：应用 × 分钟明细（稀疏 varint）
        writeVarint(out, d.appMin.size());
        for (auto& ap : d.appMin) {
            const AppMinuteData& am = ap.second;
            writeVarint(out, (uint64_t)ap.first.size());
            out.insert(out.end(), ap.first.begin(), ap.first.end());
            writeVarint(out, (uint64_t)am.keyByMinute.size());
            for (auto& mm : am.keyByMinute) {
                writeVarint(out, mm.first);
                writeVarint(out, (uint64_t)mm.second.size());
                for (auto& kv : mm.second) { writeVarint(out, kv.first); writeVarint(out, kv.second); }
            }
            writeVarint(out, (uint64_t)am.clickByMinute.size());
            for (auto& mm : am.clickByMinute) {
                writeVarint(out, mm.first);
                writeVarint(out, (uint64_t)mm.second.size());
                for (auto& g : mm.second) { writeVarint(out, g.first); writeVarint(out, g.second); }
            }
            writeVarint(out, (uint64_t)am.motionByMinute.size());
            for (auto& mm : am.motionByMinute) { writeVarint(out, mm.first); writeVarint(out, mm.second); }
            writeVarint(out, (uint64_t)am.movePxByMinute.size());
            for (auto& mm : am.movePxByMinute) { writeVarint(out, mm.first); writeVarint(out, mm.second); }
            writeVarint(out, (uint64_t)am.clickBtnMinute.size());
            for (auto& mm : am.clickBtnMinute) { writeVarint(out, mm.first); writeVarint(out, mm.second); }
            // v12：左/中/右键分钟拆分
            writeVarint(out, (uint64_t)am.leftBtnMinute.size());
            for (auto& mm : am.leftBtnMinute) { writeVarint(out, mm.first); writeVarint(out, mm.second); }
            writeVarint(out, (uint64_t)am.midBtnMinute.size());
            for (auto& mm : am.midBtnMinute) { writeVarint(out, mm.first); writeVarint(out, mm.second); }
            writeVarint(out, (uint64_t)am.rightBtnMinute.size());
            for (auto& mm : am.rightBtnMinute) { writeVarint(out, mm.first); writeVarint(out, mm.second); }
        }
    }
    out.push_back(app().darkTheme ? 1 : 0);
    writeVarint(out, app().hiddenKeys.size());
    for (uint8_t vk : app().hiddenKeys) writeVarint(out, vk);
    out.push_back(app().kbLayout & 3);
    out.push_back(app().optAppTrack ? 1 : 0);
    writeVarint(out, app().excludeApps.size());
    for (const std::string& s : app().excludeApps) {
        writeVarint(out, (uint64_t)s.size());
        out.insert(out.end(), s.begin(), s.end());
    }
    out.push_back(app().idleMin);
    out.push_back(app().lang & 1);   // 语言偏好（尾部字节，旧版本读取时缺省为 0）

    // —— LZSS 无损压缩：消除 exe 名跨区重复 + 序列化结构模式（零依赖）——
    // 压缩后立即解压校验：若解压结果与原始数据不一致，自动回退为未压缩 KMT5 格式。
    // 这保证写入的文件 100% 可正确还原——绝不因压缩 bug 导致数据丢失。
    std::vector<unsigned char> fileData;
    {
        std::vector<unsigned char> compressed;
        CompressLZSS(out.data(), out.size(), compressed);
        bool useCompressed = false;
        if (compressed.size() + 4 < out.size()) {
            // 压缩有收益：验证解压一致性
            std::vector<unsigned char> verify;
            if (DecompressLZSS(compressed.data(), compressed.size(), verify) && verify == out) {
                useCompressed = true;
            }
        }
        if (useCompressed) {
            fileData.push_back('K'); fileData.push_back('M'); fileData.push_back('T'); fileData.push_back('C');
            fileData.insert(fileData.end(), compressed.begin(), compressed.end());
        } else {
            fileData = std::move(out);
        }
    }

    // —— 原子写盘：先写临时文件并落盘，再原子替换，避免写盘中断损坏主数据文件 ——
    // 主数据文件在替换前把旧文件复制为 .bak，作为上一份可用备份（导出备份写其它路径，
    // 不做 .bak 备份，只对目标路径做原子替换）。
    if (fileData.size() > 0xFFFFFFFFull) return false;
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, fileData.data(), (DWORD)fileData.size(), &written, nullptr);
    if (ok) ok = FlushFileBuffers(h);   // 确保数据落到磁盘再替换
    CloseHandle(h);
    if (!ok || written != (DWORD)fileData.size()) { DeleteFileW(tmp.c_str()); return false; }

    bool isMain = (_wcsicmp(path.c_str(), dataFilePath().c_str()) == 0);
    if (isMain) {
        // 备份：复制当前主文件到 .bak（保持主文件在位；首次运行无主文件时失败可忽略）。
        // 用 CopyFile 而非 MoveFile，避免先把主文件移走——若下一步原子替换失败，主文件仍在。
        CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    // 仅当写的是主数据文件时更新字节缓存（导出备份写的是其它路径，不影响主文件大小）
    if (isMain) {
        g_cachedBytes = (uint64_t)fileData.size();
        g_cachedBytesValid = true;
    }
    return true;
}

// —— LZSS 无损压缩（零依赖，应用于序列化后的 v12 数据流）——
// 格式：flag 字节（8 bit，0=literal 1=match）+ token 流
// match token = u16 LE：高 3 bit = 匹配长度-3（3..10），低 13 bit = 偏移-1（1..8192）
// 窗口 = 8KB（覆盖日内 exe 名重复 + 结构模式），实测压缩率与 256KB 窗口持平但快 14 倍
// （数据冗余主要来自日内而非跨日；8KB u16 是 token 开销/窗口大小/速度的最优平衡点）
static const int LZSS_WINDOW = 8192;     // 8KB
static const int LZSS_MIN_MATCH = 3;
static const int LZSS_MAX_MATCH = 10;    // 3 bit 编码

static void CompressLZSS(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
    uint32_t orig = (uint32_t)srcLen;
    out.push_back((uint8_t)orig); out.push_back((uint8_t)(orig >> 8));
    out.push_back((uint8_t)(orig >> 16)); out.push_back((uint8_t)(orig >> 24));

    static const int HASH_SIZE = 4096;
    std::vector<int32_t> head(HASH_SIZE, -1);
    std::vector<int32_t> prev(LZSS_WINDOW, -1);
    auto hash3 = [&](size_t pos) -> int {
        return ((src[pos] * 33 + src[pos + 1]) * 33 + src[pos + 2]) & (HASH_SIZE - 1);
    };

    size_t pos = 0, flagPos = 0;
    uint8_t flagByte = 0; int flagBit = 0;
    while (pos < srcLen) {
        if (flagBit == 0) { flagPos = out.size(); out.push_back(0); }
        size_t bestLen = 0, bestOff = 0;
        if (pos + LZSS_MIN_MATCH <= srcLen) {
            int cand = head[hash3(pos)];
            int chain = 0;
            size_t maxLen = (srcLen - pos < (size_t)LZSS_MAX_MATCH) ? srcLen - pos : (size_t)LZSS_MAX_MATCH;
            while (cand >= 0 && chain < 64 && (size_t)(pos - cand) <= LZSS_WINDOW) {
                size_t len = 0;
                while (len < maxLen && src[cand + len] == src[pos + len]) ++len;
                if (len > bestLen) { bestLen = len; bestOff = (size_t)(pos - cand); }
                if (bestLen >= (size_t)LZSS_MAX_MATCH) break;
                cand = prev[cand % LZSS_WINDOW];
                ++chain;
            }
        }
        if (bestLen >= (size_t)LZSS_MIN_MATCH) {
            uint16_t tok = (uint16_t)(((bestLen - LZSS_MIN_MATCH) << 13) | (bestOff - 1));
            out.push_back((uint8_t)(tok & 0xFF)); out.push_back((uint8_t)(tok >> 8));
            out[flagPos] |= (uint8_t)(1 << flagBit);
            for (size_t i = 0; i < bestLen && pos + i + LZSS_MIN_MATCH <= srcLen; ++i) {
                int h = hash3(pos + i);
                prev[(pos + i) % LZSS_WINDOW] = head[h]; head[h] = (int32_t)(pos + i);
            }
            pos += bestLen;
        } else {
            out.push_back(src[pos]);
            if (pos + LZSS_MIN_MATCH <= srcLen) {
                int h = hash3(pos);
                prev[pos % LZSS_WINDOW] = head[h]; head[h] = (int32_t)pos;
            }
            pos += 1;
        }
        flagBit = (flagBit + 1) & 7;
    }
}

static bool DecompressLZSS(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
    if (srcLen < 4) return false;
    uint32_t origLen = src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
    if (origLen > 256 * 1024 * 1024) return false;
    out.reserve(origLen);
    size_t pos = 4; int flagBit = 8; uint8_t flagByte = 0;
    while (out.size() < origLen) {
        if (flagBit == 8) { if (pos >= srcLen) return false; flagByte = src[pos++]; flagBit = 0; }
        if (flagByte & (1 << flagBit)) {
            if (pos + 2 > srcLen) return false;
            uint16_t tok = (uint16_t)(src[pos] | (src[pos + 1] << 8)); pos += 2;
            size_t len = (size_t)(tok >> 13) + LZSS_MIN_MATCH, off = (size_t)(tok & 0x1FFF) + 1;
            if (off > out.size()) return false;
            size_t srcPos = out.size() - off;
            for (size_t i = 0; i < len && out.size() < origLen; ++i) out.push_back(out[srcPos + i]);
        } else {
            if (pos >= srcLen) return false;
            out.push_back(src[pos++]);
        }
        ++flagBit;
    }
    return out.size() == origLen;
}

static uint16_t readU16(const unsigned char*& p) {
    uint16_t v = p[0] | (p[1] << 8); p += 2; return v;
}
static uint32_t readU32(const unsigned char*& p) {
    uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= ((uint32_t)p[i]) << (8 * i); p += 4; return v;
}

// 从已加载的 days 重建累计缓存（增量维护的前提是初始为全量汇总）
static void rebuildCumulative() {
    g_cumKeys.clear();
    g_cumHeat.clear();
    for (auto& kv : app().days) {
        const DayData& d = kv.second;
        for (auto& k : d.keyCounts) g_cumKeys[k.first] += k.second;
        for (auto& h : d.mouseHeat) g_cumHeat[h.first] += h.second;
    }
}


// 读取 v12 数据（支持 KMTC 压缩格式和 KMT5 未压缩格式）。
// 版本不匹配 / 损坏文件返回 false，应用以空数据启动。
bool loadData(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, nullptr);
    if (sz < 8) { CloseHandle(h); return false; }
    std::vector<unsigned char> buf(sz);
    DWORD rd = 0;
    ReadFile(h, buf.data(), sz, &rd, nullptr);
    CloseHandle(h);

    // LZSS 解压（KMTC 格式）或直接使用（KMT5 未压缩格式）
    std::vector<unsigned char> raw;
    if (rd >= 4 && buf[0] == 'K' && buf[1] == 'M' && buf[2] == 'T' && buf[3] == 'C') {
        if (!DecompressLZSS(buf.data() + 4, rd - 4, raw)) return false;
    } else {
        raw = std::move(buf);
    }
    const unsigned char* p = raw.data();
    const unsigned char* end = p + raw.size();

    if (raw.size() < 8 || p[0] != 'K' || p[1] != 'M' || p[2] != 'T' || p[3] != '5') return false;
    p += 4;
    if (readU32(p) != KMT_DATA_VERSION) return false;   // 仅读取当前格式版本

    app().days.clear();
    app().hiddenKeys.clear();
    app().darkTheme = false;
    app().kbLayout = 0;

    uint16_t n = readU16(p);
    for (uint16_t i = 0; i < n; ++i) {
        DayData d;
        d.day = readU16(p);
        d.keys = readVarint(p, end);
        d.clicks = readVarint(p, end);
        d.mLeft = (uint32_t)readVarint(p, end);
        d.mMid = (uint32_t)readVarint(p, end);
        d.mRight = (uint32_t)readVarint(p, end);
        d.activeSec = (uint32_t)readVarint(p, end);
        d.motion = readVarint(p, end);
        d.distPx = readVarint(p, end);
        d.idleSec = (uint32_t)readVarint(p, end);
        d.maxSessionSec = (uint32_t)readVarint(p, end);
        d.sessionCount = (uint16_t)readVarint(p, end);
        uint64_t ms = readVarint(p, end);
        for (uint64_t k = 0; k < ms; ++k) {
            uint16_t min = (uint16_t)readVarint(p, end);
            d.minuteActivity[min] = (uint16_t)readVarint(p, end);
        }
        uint64_t km = readVarint(p, end);
        for (uint64_t k = 0; k < km; ++k) {
            uint16_t min = (uint16_t)readVarint(p, end);
            d.keyMinuteActivity[min] = (uint16_t)readVarint(p, end);
        }
        uint64_t cm = readVarint(p, end);
        for (uint64_t k = 0; k < cm; ++k) {
            uint16_t min = (uint16_t)readVarint(p, end);
            d.clickMinuteActivity[min] = (uint16_t)readVarint(p, end);
        }
        uint64_t kh = readVarint(p, end);
        for (uint64_t k = 0; k < kh; ++k) {
            uint32_t key = (uint32_t)readVarint(p, end);
            d.keyHourly[key] = (uint32_t)readVarint(p, end);
        }
        uint64_t ks = readVarint(p, end);
        for (uint64_t k = 0; k < ks; ++k) {
            uint8_t vk = (uint8_t)readVarint(p, end);
            d.keyCounts[vk] = (uint32_t)readVarint(p, end);
        }
        uint64_t hs = readVarint(p, end);
        for (uint64_t k = 0; k < hs; ++k) {
            uint32_t idx = (uint32_t)readVarint(p, end);
            d.mouseHeat[idx] = (uint32_t)readVarint(p, end);
        }
        for (int j = 0; j < 24; ++j) d.hourlyKeys[j] = readVarint(p, end);
        for (int j = 0; j < 24; ++j) d.hourlyClicks[j] = readVarint(p, end);
        uint64_t acn = readVarint(p, end);
        for (uint64_t k = 0; k < acn && p < end; ++k) {
            uint64_t nl = readVarint(p, end);
            std::string name;
            if (p + nl <= end) { name.assign((const char*)p, (size_t)nl); p += nl; }
            d.appCounts[name] = readVarint(p, end);
        }
        uint64_t an = readVarint(p, end);
        for (uint64_t k = 0; k < an && p < end; ++k) {
            std::string exe;
            uint64_t nl = readVarint(p, end);
            if (p + nl <= end) { exe.assign((const char*)p, (size_t)nl); p += nl; }
            AppMinuteData am;
            uint64_t kb = readVarint(p, end);
            for (uint64_t b = 0; b < kb; ++b) {
                uint16_t min = (uint16_t)readVarint(p, end);
                uint64_t vkSize = readVarint(p, end);
                auto& vkMap = am.keyByMinute[min];
                for (uint64_t v = 0; v < vkSize; ++v) {
                    uint8_t vk = (uint8_t)readVarint(p, end);
                    vkMap[vk] = (uint32_t)readVarint(p, end);
                }
            }
            uint64_t cb = readVarint(p, end);
            for (uint64_t b = 0; b < cb; ++b) {
                uint16_t min = (uint16_t)readVarint(p, end);
                uint64_t gSize = readVarint(p, end);
                auto& gMap = am.clickByMinute[min];
                for (uint64_t g = 0; g < gSize; ++g) {
                    uint32_t idx = (uint32_t)readVarint(p, end);
                    gMap[idx] = (uint32_t)readVarint(p, end);
                }
            }
            uint64_t mob = readVarint(p, end);
            for (uint64_t b = 0; b < mob; ++b) {
                uint16_t min = (uint16_t)readVarint(p, end);
                am.motionByMinute[min] = (uint16_t)readVarint(p, end);
            }
            uint64_t pxb = readVarint(p, end);
            for (uint64_t b = 0; b < pxb; ++b) {
                uint16_t min = (uint16_t)readVarint(p, end);
                am.movePxByMinute[min] = (uint32_t)readVarint(p, end);
            }
            uint64_t cbb = readVarint(p, end);
            for (uint64_t b = 0; b < cbb; ++b) {
                uint16_t min = (uint16_t)readVarint(p, end);
                am.clickBtnMinute[min] = (uint16_t)readVarint(p, end);
            }
            uint64_t lb = readVarint(p, end);
            for (uint64_t b = 0; b < lb; ++b) { uint16_t min = (uint16_t)readVarint(p, end); am.leftBtnMinute[min] = (uint16_t)readVarint(p, end); }
            uint64_t mb = readVarint(p, end);
            for (uint64_t b = 0; b < mb; ++b) { uint16_t min = (uint16_t)readVarint(p, end); am.midBtnMinute[min] = (uint16_t)readVarint(p, end); }
            uint64_t rb = readVarint(p, end);
            for (uint64_t b = 0; b < rb; ++b) { uint16_t min = (uint16_t)readVarint(p, end); am.rightBtnMinute[min] = (uint16_t)readVarint(p, end); }
            d.appMin[exe] = std::move(am);
        }
        app().days[d.day] = std::move(d);
    }
    if (p < end) {
        app().darkTheme = (*p++) != 0;
        uint64_t hn = readVarint(p, end);
        for (uint64_t k = 0; k < hn && p < end; ++k) app().hiddenKeys.insert((uint8_t)readVarint(p, end));
        if (p < end) app().kbLayout = (*p++) & 3;
        if (p < end) app().optAppTrack = (*p++) != 0;
        if (p < end) {
            uint64_t en = readVarint(p, end);
            for (uint64_t k = 0; k < en && p < end; ++k) {
                uint64_t nl = readVarint(p, end);
                if (p + nl <= end) {
                    app().excludeApps.insert(std::string((const char*)p, (size_t)nl));
                    p += nl;
                }
            }
            if (p < end) { uint8_t im = *p++; if (im >= 1 && im <= 120) app().idleMin = im; }
        }
        if (p < end) app().lang = (*p++) & 1;   // 语言偏好
    }
    rebuildCumulative();
    return true;
}

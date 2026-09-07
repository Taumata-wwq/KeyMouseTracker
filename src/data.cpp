// 数据模型与持久化
#include "data.h"
#include <shlobj.h>
#include <cstdio>
#include <algorithm>

const wchar_t* kAppName = L"KeyMouseTracker";

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

// 前置声明（eraseRange 使用）
static void rebuildCumulative();

// 历史移动里程折算：KMT4 / 旧 KMT5 无 distPx 字段（旧数据折算丢失），按“每次
// 移动采样 ≈ 平均像素”补算。比值优先取已有真实数据（distPx>0）的 distPx/motion
// 平均，无真实样本时按默认 40px/次 估算。折算结果随下次落盘持久化。
static void backfillDist() {
    uint64_t totPx = 0, totMotion = 0;
    for (auto& kv : app().days) { totPx += kv.second.distPx; totMotion += kv.second.motion; }
    uint64_t ratio = 40;
    if (totPx > 0 && totMotion > 0) { ratio = (uint64_t)((double)totPx / totMotion + 0.5); if (ratio < 1) ratio = 1; }
    bool changed = false;
    for (auto& kv : app().days) {
        if (kv.second.distPx == 0 && kv.second.motion > 0) {
            kv.second.distPx = kv.second.motion * ratio;
            changed = true;
        }
    }
    if (changed) app().dirty = true;
}

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

// KMT5：varint 编码版本。布局（与旧 KMT4 不兼容，读取时按 MAGIC 分流）：
//   "KMT5" u32 version
//   u16 dayCount
//   每 Day：u16 day
//     varint keys clicks mLeft mMid mRight activeSec motion distPx
//     varint idleSec maxSessionSec sessionCount          (v6+)
//     varint minuteActivity.size; (varint minute, varint count)*   (v6+)
//     varint keyMinuteActivity.size; (varint minute, varint count)*   (v9+)
//     varint clickMinuteActivity.size; (varint minute, varint count)*   (v9+)
//     varint keyHourly.size;      (varint key, varint count)*     (v6+)
//     varint keyCounts.size; (varint vk, varint count)*
//     varint mouseHeat.size;  (varint idx, varint count)*
//     varint hourlyKeys[24];  varint hourlyClicks[24]
//     varint appCounts.size; (varint namelen, utf8 bytes, varint count)*   (v7+)
//     varint appMin.size; { appEntry }*                                     (v11+)
//       appEntry: varint namelen, utf8 bytes,                              (与 appCounts 同名应用)
//         varint keyByMinute.size; (varint min, varint vkSize, (varint vk, varint count)*)*
//         varint clickByMinute.size; (varint min, varint gridSize, (varint idx, varint count)*)*
//         varint motionByMinute.size; (varint min, varint count)*
//         varint movePxByMinute.size; (varint min, varint px)*
//         varint clickBtnMinute.size; (varint min, varint count)*
//   全局：u8 darkTheme
//     varint hiddenKeys.size; varint vk*
//     u8 kbLayout
//     u8 optAppTrack     (v7+)
//     varint excludeApps.size + (varint namelen, utf8 bytes)* ; u8 idleMin   (v8+)
bool saveData(const std::wstring& path) {
    std::vector<unsigned char> out;
    out.insert(out.end(), { 'K', 'M', 'T', '5' });
    writeU32(out, 11); // version
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

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, out.data(), (DWORD)out.size(), &written, nullptr);
    CloseHandle(h);
    // 仅当写的是主数据文件时更新字节缓存（导出备份写的是其它路径，不影响主文件大小）
    if (ok && _wcsicmp(path.c_str(), dataFilePath().c_str()) == 0) {
        g_cachedBytes = (uint64_t)out.size();
        g_cachedBytesValid = true;
    }
    return ok != FALSE;
}

static uint16_t readU16(const unsigned char*& p) {
    uint16_t v = p[0] | (p[1] << 8); p += 2; return v;
}
static uint32_t readU32(const unsigned char*& p) {
    uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= ((uint32_t)p[i]) << (8 * i); p += 4; return v;
}
static uint64_t readU64(const unsigned char*& p) {
    uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= ((uint64_t)p[i]) << (8 * i); p += 8; return v;
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

// ===== 旧数据迁移器（v0.3.0 / 文件版本 <11）=====
// 旧数据只有“日聚合 + appCounts[exe]=键+击总数”，缺失 per-app 分钟粒度。
// 迁移目标：生成 appMin[exe] 的按分钟明细，且满足：
//   * 每应用、每日按键总分享 == appCounts 按全局键/击比例拆出的键份额；点击同理；
//   * keyMinuteActivity/clickMinuteActivity 的分钟分布忠实保留（作为权重）；
//   * 按键按 vk、点击按热力格细分（尽量利用 keyHourly / mouseHeat）；
//   * mouseHeat / keyCounts 等旧全局字段一律不动。
// 确定性：rng 由当日序号播种，结果可复现；仅对 ver<11 调用一次。

// 普通线性同余随机数（确定性）
static uint64_t rngNext(uint64_t& s) {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return s;
}

// 取某一“分钟权重表”：优先用 key/click 分钟明细，缺省回退整体活跃分钟，
// 若两者皆空则全天 1440 分钟均等权（兜底，保证不空仓）。
static std::map<uint16_t, uint32_t> pickMinuteWeights(
        const std::map<uint16_t, uint16_t>& specific,
        const std::map<uint16_t, uint16_t>& generic) {
    if (!specific.empty()) {
        std::map<uint16_t, uint32_t> w;
        for (auto& kv : specific) w[kv.first] = kv.second;
        return w;
    }
    if (!generic.empty()) {
        std::map<uint16_t, uint32_t> w;
        for (auto& kv : generic) w[kv.first] = kv.second;
        return w;
    }
    std::map<uint16_t, uint32_t> w;
    for (uint16_t m = 0; m < 1440; ++m) w[m] = 1;
    return w;
}

// 权重驱动随机分摊：把 total 个对象按 weight(min→权) 加权逐个投放，
// 返回 min→投放数（求和 == total）。weight 为空时兜底到键 0（近零数据场景）。
static std::map<uint16_t, uint32_t> scatterWeighted(
        uint64_t total, const std::map<uint16_t, uint32_t>& weight, uint64_t& rng) {
    std::map<uint16_t, uint32_t> out;
    if (total == 0) return out;
    if (weight.empty()) { out[0] = (uint32_t)total; return out; }
    // 累积权重表（递增），用于加权随机下标
    std::vector<std::pair<uint16_t, uint64_t>> cum;
    cum.reserve(weight.size());
    uint64_t acc = 0;
    for (auto& kv : weight) { acc += kv.second; cum.push_back({ kv.first, acc }); }
    for (uint64_t i = 0; i < total; ++i) {
        // r 落在 [prevAcc, curAcc) 即属于该分钟：取首个 curAcc > r 的桶
        uint64_t r = rngNext(rng) % acc;
        auto it = std::lower_bound(cum.begin(), cum.end(), r + 1,
            [](const std::pair<uint16_t, uint64_t>& e, uint64_t v) { return e.second < v; });
        out[it->first]++;
    }
    return out;
}

// 迁移单个 Day 的 appCounts → appMin 明细（不触碰旧全局字段）
static void migrateOldDay(DayData& d, uint64_t& rng) {
    if (d.appCounts.empty()) return;
    // 当日全局键/击比例（旧数据无 per-app 键击分离，按全局比例拆 appCounts 总量）
    const uint64_t dayKeys = d.keys, dayClicks = d.clicks;
    const auto keyMinW = pickMinuteWeights(d.keyMinuteActivity, d.minuteActivity);
    const auto clickMinW = pickMinuteWeights(d.clickMinuteActivity, d.minuteActivity);
    // 按键：按 vk 权重（来自 keyHourly）细分
    std::map<uint16_t, uint32_t> vkW;
    for (auto& kv : d.keyHourly) vkW[(uint8_t)(kv.first & 0xFF)] += (uint32_t)kv.second;
    if (vkW.empty()) vkW[0] = 1;   // 无每时每键数据时兜底单桶
    // 点击：按热力格权重（来自 mouseHeat）细分
    std::map<uint16_t, uint32_t> gridW;
    for (auto& kv : d.mouseHeat) gridW[kv.first] = kv.second;
    if (gridW.empty()) gridW[0] = 1;

    for (auto& ap : d.appCounts) {
        const std::string& exe = ap.first;
        const uint64_t total = ap.second;
        uint64_t keyShare, clickShare;
        if (dayKeys + dayClicks == 0) { keyShare = total / 2; clickShare = total - keyShare; }
        else {
            keyShare = (uint64_t)(((long double)total * dayKeys) / (dayKeys + dayClicks) + 0.5L);
            clickShare = total - keyShare;
        }
        AppMinuteData& am = d.appMin[exe];
        // 键：分摊到分钟 → 再按 vk 权重细化（保证 per-app 日按键总量 == keyShare）
        auto km = scatterWeighted(keyShare, keyMinW, rng);
        for (auto& mm : km) {
            auto vkMap = scatterWeighted(mm.second, vkW, rng);
            for (auto& kvk : vkMap) am.keyByMinute[mm.first][(uint8_t)kvk.first] = kvk.second;
        }
        // 击：分摊到分钟 → 再按热力格权重细化
        auto cm = scatterWeighted(clickShare, clickMinW, rng);
        for (auto& mm : cm) {
            am.clickBtnMinute[mm.first] = (uint16_t)mm.second;
            auto gMap = scatterWeighted(mm.second, gridW, rng);
            for (auto& gv : gMap) am.clickByMinute[mm.first][gv.first] = gv.second;
        }
    }
}

// 全量迁移：遍历所有日生成 appMin。返回是否产出了新明细（用于置 dirty 促落盘）。
static bool migrateOldData() {
    bool produced = false;
    for (auto& kv : app().days) {
        uint64_t rng = 0x9E3779B97F4A7C15ULL ^ ((uint64_t)kv.first * 0xDEADBEEFCAFEULL);
        size_t before = kv.second.appMin.size();
        migrateOldDay(kv.second, rng);
        if (kv.second.appMin.size() > before) produced = true;
    }
    return produced;
}

// 清空全部统计记录：默认仅清除历史与热力，保留隐藏键等偏好；随后重建累积
// 并为今天重建一个空条目，最后写盘持久化。
void clearAllData() {
    app().days.clear();
    rebuildCumulative();
    ensureCurDay();
    saveData(dataFilePath());
    app().dirty = false;
    app().needsRefresh = true;
}

// 读取 KMT4（旧，兼容）或 KMT5（varint）：
// 按 MAGIC 前 4 字节分流，两种格式共用 DayData/全局段结构。
// 校验失败返回 false，不改动已有内存数据。
bool loadData(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, nullptr);
    if (sz < 8) { CloseHandle(h); return false; }
    std::vector<unsigned char> buf(sz);
    DWORD rd = 0;
    ReadFile(h, buf.data(), sz, &rd, nullptr);
    CloseHandle(h);
    const unsigned char* p = buf.data();
    const unsigned char* end = buf.data() + rd;

    if (rd < 4 || p[0] != 'K' || p[1] != 'M' || p[2] != 'T') return false;
    bool isKMT4 = (p[3] == '4');
    bool isKMT5 = (p[3] == '5');
    if (!isKMT4 && !isKMT5) return false;
    p += 4;
    uint32_t ver = readU32(p); // version

    app().days.clear();
    app().hiddenKeys.clear();
    app().darkTheme = false;
    app().kbLayout = 0;

    if (isKMT4) {
        uint16_t n = readU16(p);
        for (uint16_t i = 0; i < n; ++i) {
            DayData d;
            d.day = readU16(p);
            d.keys = readU64(p);
            d.clicks = readU64(p);
            d.mLeft = readU32(p);
            d.mMid = readU32(p);
            d.mRight = readU32(p);
            d.activeSec = readU32(p);
            d.motion = readU64(p);
            // distPx 缺省 0（KMT4 无此字段）
            uint16_t ks = readU16(p);
            for (uint16_t k = 0; k < ks; ++k) { uint8_t vk = *p++; uint32_t c = readU32(p); d.keyCounts[vk] = c; }
            uint16_t hs = readU16(p);
            for (uint16_t k = 0; k < hs; ++k) { uint32_t idx = readU32(p); uint32_t c = readU32(p); d.mouseHeat[idx] = c; }
            for (int j = 0; j < 24; ++j) d.hourlyKeys[j] = readU64(p);
            for (int j = 0; j < 24; ++j) d.hourlyClicks[j] = readU64(p);
            app().days[d.day] = std::move(d);
        }
        if (p < end) {
            app().darkTheme = (*p++) != 0;
            uint16_t hn = readU16(p);
            for (uint16_t k = 0; k < hn && p < end; ++k) app().hiddenKeys.insert(*p++);
            if (p < end) app().kbLayout = (*p++) & 3;
        }
    } else { // KMT5
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
            if (ver >= 6) {
                d.idleSec = (uint32_t)readVarint(p, end);
                d.maxSessionSec = (uint32_t)readVarint(p, end);
                d.sessionCount = (uint16_t)readVarint(p, end);
                uint64_t ms = readVarint(p, end);
                for (uint64_t k = 0; k < ms; ++k) {
                    uint16_t min = (uint16_t)readVarint(p, end);
                    d.minuteActivity[min] = (uint16_t)readVarint(p, end);
                }
                // v9+：分钟级按键/点击分离。
                // 注意读取顺序必须与 saveData 完全一致：
                // minuteActivity → keyMinuteActivity → clickMinuteActivity → keyHourly
                if (ver >= 9) {
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
                }
                uint64_t kh = readVarint(p, end);
                for (uint64_t k = 0; k < kh; ++k) {
                    uint32_t key = (uint32_t)readVarint(p, end);
                    d.keyHourly[key] = (uint32_t)readVarint(p, end);
                }
            }
            uint64_t ks = readVarint(p, end);
            for (uint64_t k = 0; k < ks; ++k) {
                uint8_t vk = (uint8_t)readVarint(p, end);
                uint64_t c = readVarint(p, end);
                d.keyCounts[vk] = (uint32_t)c;
            }
            uint64_t hs = readVarint(p, end);
            for (uint64_t k = 0; k < hs; ++k) {
                uint32_t idx = (uint32_t)readVarint(p, end);
                uint64_t c = readVarint(p, end);
                d.mouseHeat[idx] = (uint32_t)c;
            }
            for (int j = 0; j < 24; ++j) d.hourlyKeys[j] = readVarint(p, end);
            for (int j = 0; j < 24; ++j) d.hourlyClicks[j] = readVarint(p, end);
            if (ver >= 7) {
                uint64_t acn = readVarint(p, end);
                for (uint64_t k = 0; k < acn && p < end; ++k) {
                    uint64_t nl = readVarint(p, end);
                    std::string name;
                    if (p + nl <= end) { name.assign((const char*)p, (size_t)nl); p += nl; }
                    d.appCounts[name] = readVarint(p, end);
                }
                // v11+：应用 × 分钟明细（固定分层编码，读取顺序与 saveData 一致）
                if (ver >= 11) {
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
                        d.appMin[exe] = std::move(am);
                    }
                }
            }
            app().days[d.day] = std::move(d);
        }
        if (p < end) {
            app().darkTheme = (*p++) != 0;
            uint64_t hn = readVarint(p, end);
            for (uint64_t k = 0; k < hn && p < end; ++k) app().hiddenKeys.insert((uint8_t)readVarint(p, end));
            if (p < end) app().kbLayout = (*p++) & 3;
            if (p < end) app().optAppTrack = (*p++) != 0;
            if (p < end && ver >= 8) {
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
        }
    }
    rebuildCumulative();
    backfillDist();   // 旧数据缺 distPx 时按移动采样折算里程

    // 清理历史越界分钟键：分钟序号合法区间 0..1439（当日分钟）。
    // 历史版本异常进程可能残留 >1439 的键，导出会显示 42:48 这类错误时间；
    // 此处删除并标记 dirty，由定时保存将干净数据写盘（导入场景不写源文件）。
    bool minuteFixed = false;
    for (auto& kv : app().days) {
        DayData& d = kv.second;
        for (auto it = d.minuteActivity.begin(); it != d.minuteActivity.end();)
            it = (it->first <= 1439) ? ++it : (minuteFixed = true, d.minuteActivity.erase(it));
        for (auto it = d.keyMinuteActivity.begin(); it != d.keyMinuteActivity.end();)
            it = (it->first <= 1439) ? ++it : (minuteFixed = true, d.keyMinuteActivity.erase(it));
        for (auto it = d.clickMinuteActivity.begin(); it != d.clickMinuteActivity.end();)
            it = (it->first <= 1439) ? ++it : (minuteFixed = true, d.clickMinuteActivity.erase(it));
    }
    if (minuteFixed) app().dirty = true;

    // v11 迁移：仅旧格式（ver<11）触发，生成 per-app 分钟明细；产出后置 dirty
    // 以便下次落盘升级为 v11。KMT4 亦走此分支（appCounts 通常为空则无操作）。
    if (ver < 11) {
        app().migrated = true;
        if (migrateOldData()) app().dirty = true;
    }
    return true;
}
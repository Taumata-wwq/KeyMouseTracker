// 数据模型与持久化
#include "data.h"
#include <shlobj.h>
#include <cstdio>
#include <cstring>

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

// 前台应用归因（仅 optAppTrack 开启）：按键/点击计入当前前台进程（排除列表内不计）
static void attrApp(DayData& t) {
    if (!app().optAppTrack) return;
    if (g_foreApp.empty()) return;
    if (app().excludeApps.count(g_foreApp)) return;
    t.appCounts[g_foreApp]++;
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
    touch();
}

void recordMove() {
    if (app().paused) return;
    refreshClock();
    ensureCurDay();
    DayData& t = app().days[app().cur];
    t.motion++;
    if ((t.motion & 0x1F) == 0) { app().lastActivity = GetTickCount(); app().needsRefresh = true; }
}

void recordMoveDist(uint64_t px) {
    if (app().paused) return;
    refreshClock();
    ensureCurDay();
    app().days[app().cur].distPx += px;
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

// 重建累计缓存（增量维护的前提是初始为全量汇总）。前置声明供 eraseRange 使用
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
//   全局：u8 darkTheme
//     varint hiddenKeys.size; varint vk*
//     u8 kbLayout
//     u8 optAppTrack     (v7+)
//     varint excludeApps.size + (varint namelen, utf8 bytes)* ; u8 idleMin   (v8+)
bool saveData(const std::wstring& path) {
    std::vector<unsigned char> out;
    out.insert(out.end(), { 'K', 'M', 'T', '5' });
    writeU32(out, 10); // version
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
    return true;
}
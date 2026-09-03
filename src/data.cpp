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
static DWORD g_cachedTick = 0;

static void refreshClock() {
    DWORD now = GetTickCount();
    if (g_cachedDay >= 0 && (now - g_cachedTick) < 500) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    g_cachedDay = days_from_civil((int)st.wYear, st.wMonth, st.wDay) - kBase;
    g_cachedHour = (int)st.wHour;
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

void recordKey(uint8_t vk) {
    if (app().paused) return;
    refreshClock();
    ensureCurDay();
    DayData& t = app().days[app().cur];
    t.keyCounts[vk]++;
    t.keys++;
    if (g_cachedHour >= 0 && g_cachedHour < 24) t.hourlyKeys[g_cachedHour]++;
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
    int idx = heatIndexFromScreen(x, y);
    if (idx >= 0) { t.mouseHeat[idx]++; g_cumHeat[idx]++; }
    if (g_cachedHour >= 0 && g_cachedHour < 24) t.hourlyClicks[g_cachedHour]++;
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

static void writeU16(std::vector<unsigned char>& out, uint16_t v) {
    out.push_back((unsigned char)(v & 0xFF));
    out.push_back((unsigned char)((v >> 8) & 0xFF));
}
static void writeU32(std::vector<unsigned char>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}
static void writeU64(std::vector<unsigned char>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}

bool saveData(const std::wstring& path) {
    std::vector<unsigned char> out;
    // MAGIC "KMT4"：逐日聚合 + 小时级统计 + 全局隐藏键 + 主题偏好
    out.insert(out.end(), { 'K', 'M', 'T', '4' });
    writeU32(out, 4); // version
    auto& days = app().days;
    writeU16(out, (uint16_t)days.size());
    for (auto it = days.begin(); it != days.end(); ++it) {
        const DayData& d = it->second;
        writeU16(out, d.day);
        writeU64(out, d.keys);
        writeU64(out, d.clicks);
        writeU32(out, d.mLeft);
        writeU32(out, d.mMid);
        writeU32(out, d.mRight);
        writeU32(out, d.activeSec);
        writeU64(out, d.motion);
        writeU16(out, (uint16_t)d.keyCounts.size());
        for (auto& kv : d.keyCounts) {
            out.push_back(kv.first);
            writeU32(out, kv.second);
        }
        writeU16(out, (uint16_t)d.mouseHeat.size());
        for (auto& kv : d.mouseHeat) {
            writeU32(out, kv.first);   // 格子索引
            writeU32(out, kv.second);  // 次数
        }
        for (int i = 0; i < 24; ++i) writeU64(out, d.hourlyKeys[i]);
        for (int i = 0; i < 24; ++i) writeU64(out, d.hourlyClicks[i]);
    }
    // 全局段：主题偏好 + 隐藏键集合 + 键盘配列
    out.push_back(app().darkTheme ? 1 : 0);
    writeU16(out, (uint16_t)app().hiddenKeys.size());
    for (uint8_t vk : app().hiddenKeys) out.push_back(vk);
    out.push_back(app().kbLayout & 3);

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, out.data(), (DWORD)out.size(), &written, nullptr);
    CloseHandle(h);
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

// 读取 KMT4：逐日聚合 + 小时级统计 + 全局隐藏键 + 主题偏好
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

    // 仅识别当前格式（校验失败返回 false，不改动已有内存数据）
    if (rd < 4 || p[0] != 'K' || p[1] != 'M' || p[2] != 'T' || p[3] != '4') return false;
    p += 4;
    readU32(p); // version
    uint16_t n = readU16(p);
    app().days.clear();
    app().hiddenKeys.clear();
    app().darkTheme = false;
    app().kbLayout = 0;
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
        uint16_t ks = readU16(p);
        for (uint16_t k = 0; k < ks; ++k) {
            uint8_t vk = *p++;
            uint32_t c = readU32(p);
            d.keyCounts[vk] = c;
        }
        uint16_t hs = readU16(p);
        for (uint16_t k = 0; k < hs; ++k) {
            uint32_t idx = readU32(p);
            uint32_t c = readU32(p);
            d.mouseHeat[idx] = c;
        }
        for (int j = 0; j < 24; ++j) d.hourlyKeys[j] = readU64(p);
        for (int j = 0; j < 24; ++j) d.hourlyClicks[j] = readU64(p);
        app().days[d.day] = std::move(d);
    }
    if (p < end) {
        app().darkTheme = (*p++) != 0;
        uint16_t hn = readU16(p);
        for (uint16_t k = 0; k < hn && p < end; ++k) app().hiddenKeys.insert(*p++);
        // 尾随字节：键盘配列（旧版数据文件无此字段，保持默认 108 键）
        if (p < end) app().kbLayout = (*p++) & 3;
    }
    rebuildCumulative();
    return true;
}
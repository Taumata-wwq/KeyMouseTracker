// 应用筛选 + 明细范围 查询上下文（自 main.cpp 抽离）。
#include "stats_query.h"
#include "uiutil.h"
#include <set>
#include <vector>
#include <algorithm>

// —— 状态（extern 供 main.cpp 访问；内部状态为 static）——
std::string g_filterApp;
bool        g_filterCacheDirty = true;
DWORD       g_filterCacheTick = 0;
bool        g_detailAllTime = true;
bool        g_detailDirty = true;
std::string g_detailJson;
uint32_t    g_detailCacheTick = 0;

static std::map<uint8_t, uint32_t> g_filterKeys;    // 筛选应用全历史按键（按 vk）
static std::map<uint32_t, uint32_t> g_filterHeat;   // 筛选应用全历史鼠标热力（按格）
static int64_t g_detailStartMin = 0;
static int64_t g_detailEndMin = -1;                 // -1=不限

static void rebuildFilterCache() {
    g_filterCacheDirty = false;
    g_filterCacheTick = GetTickCount();
    g_filterKeys.clear();
    g_filterHeat.clear();
    for (auto& kv : app().days) {
        auto it = kv.second.appMin.find(g_filterApp);
        if (it == kv.second.appMin.end()) continue;
        const AppMinuteData& am = it->second;
        for (auto& mm : am.keyByMinute)
            for (auto& kvk : mm.second) g_filterKeys[kvk.first] += kvk.second;
        for (auto& mm : am.clickByMinute)
            for (auto& g : mm.second) g_filterHeat[g.first] += g.second;
    }
}

// 惰性取数入口：缓存失效时才重建（g_filterApp 变化或数据更新后）
const std::map<uint8_t, uint32_t>& keysForApp(const std::string& exe) {
    if (g_filterCacheDirty) rebuildFilterCache();
    (void)exe;
    return g_filterKeys;
}
const std::map<uint32_t, uint32_t>& heatForApp(const std::string& exe) {
    if (g_filterCacheDirty) rebuildFilterCache();
    (void)exe;
    return g_filterHeat;
}

// 指定应用在 [ms, me) 分钟区间内的活跃分钟数（keyByMinute/clickByMinute/
// motionByMinute 分钟键并集计数；分钟 0..1439 用位图标记，避免逐分钟遍历）。
int appActiveMinutes(const DayData& d, const std::string& exe, int ms, int me) {
    auto it = d.appMin.find(exe);
    if (it == d.appMin.end() || me <= ms) return 0;
    const AppMinuteData& am = it->second;
    uint64_t bits[23] = {};
    auto mark = [&](uint16_t m) { if (m < 1440) bits[m >> 6] |= (1ULL << (m & 63)); };
    for (auto& kv : am.keyByMinute)    if (kv.first >= (uint16_t)ms && kv.first < (uint16_t)me) mark(kv.first);
    for (auto& kv : am.clickByMinute)  if (kv.first >= (uint16_t)ms && kv.first < (uint16_t)me) mark(kv.first);
    for (auto& kv : am.motionByMinute) if (kv.first >= (uint16_t)ms && kv.first < (uint16_t)me) mark(kv.first);
    int n = 0;
    for (int m = ms; m < me; ++m) if (bits[m >> 6] & (1ULL << (m & 63))) ++n;
    return n;
}

// 解析明细范围（"YYYY-MM-DD HH:MM" 起止；空串=不限）；成功后切到非 allTime 并置脏
void applyDetailRange(const std::string& s1, const std::string& s2) {
    int y1 = 0, m1 = 0, d1 = 0, y2 = 0, m2 = 0, d2 = 0;
    bool has1 = parseYMDLoose(s1, 8, y1, m1, d1);
    bool has2 = parseYMDLoose(s2, 8, y2, m2, d2);
    if (!has1 && !has2) {
        g_detailAllTime = true;   // 全空 = 全部时间
        g_detailDirty = true;
        return;
    }
    int h1 = 0, n1 = 0, h2 = 23, n2 = 59;
    if (has1) { parseDetailHHMM(s1, h1, n1); g_detailStartMin = (int64_t)dayIndexFromYMD(y1, m1, d1) * 1440 + h1 * 60 + n1; }
    else g_detailStartMin = 0;
    if (has2) { parseDetailHHMM(s2, h2, n2); g_detailEndMin = (int64_t)dayIndexFromYMD(y2, m2, d2) * 1440 + h2 * 60 + n2; }
    else g_detailEndMin = (int64_t)1 << 50;
    if (g_detailStartMin > g_detailEndMin) { int64_t t = g_detailStartMin; g_detailStartMin = g_detailEndMin; g_detailEndMin = t; }
    g_detailAllTime = false;
    g_detailDirty = true;
}

std::string buildDetailJson() {
    int64_t sMin = g_detailAllTime ? 0 : g_detailStartMin;
    int64_t eMin = g_detailAllTime ? ((int64_t)1 << 50) : g_detailEndMin;
    std::map<uint8_t, uint64_t> kc;      // vk -> 次数（按键排行）
    uint64_t kbdSum = 0;
    uint64_t mL = 0, mR = 0, mM = 0, totalClicks = 0;
    uint64_t distPx = 0, actSec = 0, idleSec = 0, maxSessSec = 0, sessCnt = 0;
    const bool filtered = !g_filterApp.empty();
    // 筛选应用活跃分钟（绝对分钟，用于活跃时长与最长连续活跃段）
    std::set<int64_t> appActiveAbs;

    for (auto& kv : app().days) {
        int dayIdx = (int)kv.first;
        int64_t day0 = (int64_t)dayIdx * 1440;
        int64_t day1 = day0 + 1439;
        if (day1 < sMin || day0 > eMin) continue;   // 与范围无交集
        const DayData& d = kv.second;
        int64_t win0 = std::max(sMin, day0);
        int64_t win1 = std::min(eMin, day1);
        int h0 = (int)((win0 - day0) / 60), h1 = (int)((win1 - day0) / 60);

        // 键盘 per-key
        if (filtered) {
            auto am = d.appMin.find(g_filterApp);
            if (am != d.appMin.end()) {
                for (auto& mm : am->second.keyByMinute) {
                    int64_t absMin = day0 + mm.first;
                    if (absMin < sMin || absMin > eMin) continue;
                    for (auto& kv2 : mm.second) { kc[kv2.first] += kv2.second; kbdSum += kv2.second; }
                }
            }
        } else {
            // keyHourly 复合键 = hour*256 + vk，恒记录（小时粒度；边界时段按小时对齐）
            for (auto& kh : d.keyHourly) {
                int h = (int)(kh.first >> 8);
                if (h >= h0 && h <= h1) { kc[(uint8_t)(kh.first & 0xFF)] += kh.second; kbdSum += kh.second; }
            }
        }

        if (filtered) {
            // 按应用：全部维度取自该应用 appMin 分钟明细（修复：此前里程/活跃误用全局日汇总）
            auto am = d.appMin.find(g_filterApp);
            if (am != d.appMin.end()) {
                const AppMinuteData& a = am->second;
                auto inWin = [&](uint16_t mn) { int64_t abs = day0 + mn; return abs >= sMin && abs <= eMin; };
                for (auto& cb : a.clickBtnMinute) if (inWin(cb.first)) totalClicks += cb.second;
                for (auto& lb : a.leftBtnMinute)  if (inWin(lb.first)) mL += lb.second;
                for (auto& mb : a.midBtnMinute)   if (inWin(mb.first)) mM += mb.second;
                for (auto& rb : a.rightBtnMinute) if (inWin(rb.first)) mR += rb.second;
                for (auto& px : a.movePxByMinute) if (inWin(px.first)) distPx += px.second;
                for (auto& mm : a.keyByMinute)    if (inWin(mm.first)) appActiveAbs.insert(day0 + mm.first);
                for (auto& mm : a.clickByMinute)  if (inWin(mm.first)) appActiveAbs.insert(day0 + mm.first);
                for (auto& mm : a.movePxByMinute) if (inWin(mm.first)) appActiveAbs.insert(day0 + mm.first);
            }
        } else {
            mL += d.mLeft; mR += d.mRight; mM += d.mMid;
            distPx += d.distPx;
            actSec += d.activeSec;
            idleSec += d.idleSec;
            if ((uint64_t)d.maxSessionSec > maxSessSec) maxSessSec = d.maxSessionSec;
            sessCnt += d.sessionCount;
        }
    }
    if (!filtered) totalClicks = mL + mR + mM;
    else {
        // 活跃时长 ≈ 活跃分钟 × 60；最长连续活跃 = 最长连续分钟串 × 60（跨午夜自然断裂）
        actSec = appActiveAbs.size() * 60;
        uint64_t run = 0, best = 0;
        int64_t prev = INT64_MIN;
        for (int64_t m : appActiveAbs) {
            run = (m == prev + 1) ? run + 1 : 1;
            if (run > best) best = run;
            prev = m;
        }
        maxSessSec = best * 60;
    }

    // 按键排行：按次数降序
    std::vector<std::pair<uint8_t, uint64_t>> sorted(kc.begin(), kc.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<uint8_t, uint64_t>& a, const std::pair<uint8_t, uint64_t>& b) {
                  return a.second > b.second;
              });
    // 条宽归一化基准：峰值 × 1.33 让 top 行停在 ~75% 宽（避免高频键堆满、无对比）。
    // core-ui 的 progressbar 不响应动态 :max 绑定，故 w 字段在 C++ 侧直接归一化到 0-100。
    uint64_t peak = sorted.empty() ? 0 : sorted[0].second;
    double kbdMax = std::max(peak * 1.33, 1.0);
    double mouseMax = std::max({ (double)mL, (double)mR, (double)mM, 1.0 }) * 1.33;
    auto normW = [](uint64_t v, double mx) {
        if (mx <= 0.0) return 0;
        double p = (double)v * 100.0 / mx;
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        return (int)(p + 0.5);
    };
    std::string out = "{\"kbd\":[";
    char lbuf[32];
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i) out += ",";
        out += "{\"l\":\"" + jsonEscape(vkLabel(sorted[i].first, lbuf)) + "\""
             + ",\"c\":" + std::to_string(sorted[i].second)
             + ",\"w\":" + std::to_string(normW(sorted[i].second, kbdMax)) + "}";
    }
    out += "],\"kbdSum\":" + std::to_string(kbdSum) +
           ",\"left\":" + std::to_string(mL) + ",\"right\":" + std::to_string(mR) + ",\"mid\":" + std::to_string(mM) +
           ",\"leftW\":" + std::to_string(normW(mL, mouseMax)) +
           ",\"rightW\":" + std::to_string(normW(mR, mouseMax)) +
           ",\"midW\":" + std::to_string(normW(mM, mouseMax)) +
           ",\"total\":" + std::to_string(totalClicks) +
           ",\"distCm\":" + std::to_string(distToCm(distPx)) +
           ",\"activeSec\":" + std::to_string(actSec) + ",\"idleSec\":" + std::to_string(idleSec) +
           ",\"maxSessionSec\":" + std::to_string(maxSessSec) + ",\"sessionCount\":" + std::to_string(sessCnt) + "}";
    return out;
}

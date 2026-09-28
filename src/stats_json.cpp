// 统计 JSON 构建（自 main.cpp 抽离）：只读 app() 数据，无筛选/明细全局依赖。
#include "stats_json.h"
#include "data.h"
#include <vector>
#include <map>
#include <algorithm>
#include <string>

// 今日概况：{keys, clicks, motion, distCm, activeSec}
std::string BuildTodayJson() {
    auto it = app().days.find(app().cur);
    if (it == app().days.end())
        return "{\"keys\":0,\"clicks\":0,\"motion\":0,\"distCm\":0,\"activeSec\":0}";
    const DayData& d = it->second;
    return "{\"keys\":" + std::to_string(d.keys) + ",\"clicks\":" + std::to_string(d.clicks) +
           ",\"motion\":" + std::to_string(d.motion) + ",\"distCm\":" + std::to_string(distToCm(d.distPx)) +
           ",\"activeSec\":" + std::to_string(d.activeSec) + "}";
}

// 累计概况：{keys, clicks, days, activeSec, distCm}
std::string BuildTotalJson() {
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

// 存储概况：数据文件字节数（缓存）、覆盖天数、最早/最晚日期
std::string BuildStorageJson() {
    StorageInfo si = storageInfo();
    return "{\"bytes\":" + std::to_string(si.bytes) +
           ",\"days\":" + std::to_string(si.days) +
           ",\"first\":\"" + (si.days ? dayIndexToStr(si.first) : "") + "\"" +
           ",\"last\":\"" + (si.days ? dayIndexToStr(si.last) : "") + "\"}";
}

// 前台应用排除列表（v-for 数据源）
std::string BuildExcludeListJson() {
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

// 今日活跃应用 Top6 JSON（仅采集开启且有当日数据时非空）：[{"n":..,"c":..,"p":..}, ...]
std::string BuildTopAppsJson() {
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

// 24h 应用排行（今日 + 昨日 appCounts 合并，排除列表内不计）。
// 综合分：48 整点桶（昨日 24 + 今日 24）逐桶归一化加权求和 + EMA 平滑，相对评分映射到 0..100。
static std::map<std::string, double> g_appScoreEma;   // 跨刷新的 EMA 状态

std::string BuildApps24hJson() {
    auto& days = app().days;
    auto it = days.find(app().cur);
    if (!app().optAppTrack || it == days.end()) return "[]";
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

    // 固定参考（每整点桶"满活跃"的参考量），归一化到 [0,1] 后加权融合
    constexpr double REF_KEYS = 400.0, REF_CLICKS = 200.0, REF_MOTION_CM = 4000.0;
    constexpr double W_K = 0.28, W_C = 0.27, W_M = 0.20, W_A = 0.25;
    constexpr double EMA_ALPHA = 0.25;

    auto bucketScore = [&](const DayData& d, const std::string& exe, int h) {
        double nk = std::min(1.0, (double)appKeys(d, exe, h * 60, h * 60 + 59) / REF_KEYS);
        double nc = std::min(1.0, (double)appClicks(d, exe, h * 60, h * 60 + 59) / REF_CLICKS);
        double nm = std::min(1.0, (double)distToCm(appMotionPx(d, exe, h * 60, h * 60 + 59)) / REF_MOTION_CM);
        double na = std::min(1.0, (double)appActiveMin(d, exe, h * 60, h * 60 + 59) / 60.0);
        return W_K * nk + W_C * nc + W_M * nm + W_A * na;
    };

    struct Row {
        std::string n;
        uint64_t c = 0;
        uint64_t k = 0, cl = 0, m = 0, a = 0;
        double score = 0.0;
    };
    std::vector<Row> rows;
    rows.reserve(apps24h.size());
    for (auto& kv : apps24h) {
        Row r;
        r.n = kv.first;
        r.c = kv.second;
        if (it != days.end()) {
            r.k  += appKeys(it->second,  r.n, 0, 1439);
            r.cl += appClicks(it->second, r.n, 0, 1439);
            r.m  += distToCm(appMotionPx(it->second, r.n, 0, 1439));
            r.a  += appActiveMin(it->second, r.n, 0, 1439);
        }
        if (yit != days.end()) {
            r.k  += appKeys(yit->second,  r.n, 0, 1439);
            r.cl += appClicks(yit->second, r.n, 0, 1439);
            r.m  += distToCm(appMotionPx(yit->second, r.n, 0, 1439));
            r.a  += appActiveMin(yit->second, r.n, 0, 1439);
        }
        // 48 整点桶（昨日 24 + 今日 24）逐桶归一化加权分求和
        double raw = 0.0;
        if (yit != days.end()) for (int h = 0; h < 24; ++h) raw += bucketScore(yit->second, r.n, h);
        for (int h = 0; h < 24; ++h) raw += bucketScore(it->second, r.n, h);
        // EMA 平滑（跨刷新，抑制抖动；首次直接对齐）
        double prev = g_appScoreEma.count(r.n) ? g_appScoreEma[r.n] : raw;
        double cur = prev + (raw - prev) * EMA_ALPHA;
        g_appScoreEma[r.n] = cur;
        r.score = cur;
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& x, const Row& y) {
                  if (x.score != y.score) return x.score > y.score;
                  return x.c > y.c;
              });
    // 相对评分：第一名 = 100，其余按比例重新打分
    double maxScore = 0.0;
    for (auto& r : rows) if (r.score > maxScore) maxScore = r.score;
    std::string out = "[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) out += ",";
        const Row& r = rows[i];
        int score = maxScore > 0.0 ? (int)(r.score / maxScore * 100.0 + 0.5) : 0;
        out += "{\"n\":\"" + jsonEscape(r.n.c_str()) + "\",\"c\":" + std::to_string(r.c) +
               ",\"s\":" + std::to_string(score) +
               ",\"k\":" + std::to_string(r.k) +
               ",\"cl\":" + std::to_string(r.cl) +
               ",\"m\":" + std::to_string(r.m) +
               ",\"a\":" + std::to_string(r.a) + "}";
    }
    out += "]";
    return out;
}

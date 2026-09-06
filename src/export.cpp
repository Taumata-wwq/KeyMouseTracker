// CSV / JSON 导出：纯文本拼接，零第三方依赖
#include "export.h"
#include "data.h"
#include <windows.h>
#include <cstdio>
#include <vector>
#include <string>
#include <set>

static bool writeFileBytes(const std::wstring& path, const std::vector<unsigned char>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}

// CSV 字段转义：含逗号/引号/换行时用双引号包裹并加倍内部引号（Excel 兼容）
static std::string csvEscape(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string r = "\"";
    for (char c : s) { if (c == '"') r += "\"\""; else r += c; }
    r += "\"";
    return r;
}

// 当前时间字符串 "YYYY-MM-DD HH:MM"
static std::string nowStr() {
    SYSTEMTIME st; GetLocalTime(&st);
    char b[32];
    snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return b;
}

// 分钟序号(0..1439) → "HH:MM"
static std::string minToHHMM(int m) {
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", m / 60, m % 60);
    return b;
}

// CSV 导出：区块化多表，覆盖全部存储维度——
//   每日汇总 / 按键×小时矩阵 / 按键分布 / 鼠标热力 / 前台应用 / 分钟级活跃。
// 范围 [startIdx, endIdx] 含边界；全量传 0..65535。
bool ExportCSV(const std::wstring& path, int startIdx, int endIdx) {
    std::string out;
    out += "\xEF\xBB\xBF"; // UTF-8 BOM
    out += "# KeyMouseTracker 数据导出\r\n";
    out += "# 导出时间: " + nowStr() + "\r\n";
    out += "# 数据范围: " + dayIndexToStr(startIdx) + " ~ " + dayIndexToStr(endIdx) + "\r\n";
    out += "# 应用统计跟踪: " + std::string(app().optAppTrack ? "开启" : "关闭") + "\r\n";
    out += "# 空闲阈值: " + std::to_string((int)app().idleMin) + " 分钟\r\n";
    out += "\r\n";

    // 1) 每日汇总
    out += "=== 每日汇总 ===\r\n";
    out += "日期,按键,点击,左键,中键,右键,位移像素,位移厘米,移动次数,活跃秒,空闲秒,最长活跃段秒,活跃段次数\r\n";
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        const DayData& d = kv.second;
        out += dayIndexToStr(idx) + ",";
        out += std::to_string(d.keys) + ",";
        out += std::to_string(d.clicks) + ",";
        out += std::to_string(d.mLeft) + ",";
        out += std::to_string(d.mMid) + ",";
        out += std::to_string(d.mRight) + ",";
        out += std::to_string(d.distPx) + ",";
        out += std::to_string(distToCm(d.distPx)) + ",";
        out += std::to_string(d.motion) + ",";
        out += std::to_string(d.activeSec) + ",";
        out += std::to_string(d.idleSec) + ",";
        out += std::to_string(d.maxSessionSec) + ",";
        out += std::to_string(d.sessionCount) + "\r\n";
    }
    out += "\r\n";

    // 2) 按键×小时矩阵（列 = 范围内全部按键并集，行 = 日期+小时）
    std::set<uint8_t> ks;
    for (auto& kv : app().days) {
        if ((int)kv.first < startIdx || (int)kv.first > endIdx) continue;
        for (auto& kh : kv.second.keyHourly) ks.insert((uint8_t)(kh.first & 0xFF));
    }
    out += "=== 按键×小时矩阵 ===\r\n";
    out += "日期,小时";
    for (uint8_t k : ks) { char kb[32]; out += "," + csvEscape(vkLabel(k, kb)); }
    out += ",点击\r\n";
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        const DayData& d = kv.second;
        std::string ds = dayIndexToStr(idx);
        for (int h = 0; h < 24; ++h) {
            char hb[8];
            snprintf(hb, sizeof(hb), "%02d", h);
            out += ds + "," + hb;
            for (uint8_t k : ks) {
                auto it = d.keyHourly.find((uint32_t)h * 256 + k);
                out += "," + std::to_string(it != d.keyHourly.end() ? it->second : 0);
            }
            out += "," + std::to_string(d.hourlyClicks[h]);
            out += "\r\n";
        }
    }
    out += "\r\n";

    // 3) 按键分布（当日各键总次数）
    out += "=== 按键分布 ===\r\n";
    out += "日期,键码,键名,次数\r\n";
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        std::string ds = dayIndexToStr(idx);
        for (auto& kc : kv.second.keyCounts) {
            char kb[32];
            out += ds + "," + std::to_string((int)kc.first) + "," + csvEscape(vkLabel(kc.first, kb)) +
                   "," + std::to_string(kc.second) + "\r\n";
        }
    }
    out += "\r\n";

    // 4) 鼠标热力（屏幕网格 48×27：网格列 = 索引%48，网格行 = 索引/48）
    out += "=== 鼠标热力 ===\r\n";
    out += "日期,区域索引,网格列,网格行,次数\r\n";
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        std::string ds = dayIndexToStr(idx);
        for (auto& mh : kv.second.mouseHeat)
            out += ds + "," + std::to_string(mh.first) + "," + std::to_string(mh.first % kHeatW) +
                   "," + std::to_string(mh.first / kHeatW) + "," + std::to_string(mh.second) + "\r\n";
    }
    out += "\r\n";

    // 5) 前台应用（仅 optAppTrack 开启时有数据）
    out += "=== 前台应用 ===\r\n";
    out += "日期,应用,按键点击次数\r\n";
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        std::string ds = dayIndexToStr(idx);
        for (auto& ac : kv.second.appCounts)
            out += ds + "," + csvEscape(ac.first) + "," + std::to_string(ac.second) + "\r\n";
    }
    out += "\r\n";

    // 6) 分钟级活跃（仅存非零分钟；总次数 = 按键 + 点击）
    out += "=== 分钟级活跃 ===\r\n";
    out += "日期,时间,总次数,按键,点击\r\n";
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        const DayData& d = kv.second;
        std::string ds = dayIndexToStr(idx);
        std::set<uint16_t> mins;
        // 分钟键合法区间为 0..1439（当日分钟）。历史版本数据文件可能残留越界键，
        // 无法对应任何真实时间点，导出时过滤以免生成 42:48 这类错误时间。
        for (auto& m : d.minuteActivity) if (m.first <= 1439) mins.insert(m.first);
        for (auto& m : d.keyMinuteActivity) if (m.first <= 1439) mins.insert(m.first);
        for (auto& m : d.clickMinuteActivity) if (m.first <= 1439) mins.insert(m.first);
        for (uint16_t m : mins) {
            auto itT = d.minuteActivity.find(m);
            auto itK = d.keyMinuteActivity.find(m);
            auto itC = d.clickMinuteActivity.find(m);
            out += ds + "," + minToHHMM(m) + ",";
            out += std::to_string(itT != d.minuteActivity.end() ? itT->second : 0) + ",";
            out += std::to_string(itK != d.keyMinuteActivity.end() ? itK->second : 0) + ",";
            out += std::to_string(itC != d.clickMinuteActivity.end() ? itC->second : 0) + "\r\n";
        }
    }

    std::vector<unsigned char> bytes(out.begin(), out.end());
    return writeFileBytes(path, bytes);
}

// JSON 导出：完整保真（DayData 全字段），供程序化分析。范围 [startIdx, endIdx]
bool ExportJSON(const std::wstring& path, int startIdx, int endIdx) {
    std::string out = "{";
    out += "\"app\":\"KeyMouseTracker\",\"export\":\"json\",\"format\":2,";
    out += "\"exportTime\":\"" + nowStr() + "\",";
    out += "\"range\":{\"start\":\"" + dayIndexToStr(startIdx) + "\",\"end\":\"" + dayIndexToStr(endIdx) + "\"},";
    out += "\"meta\":{\"optAppTrack\":" + std::to_string(app().optAppTrack ? 1 : 0) + ",";
    out += "\"idleMin\":" + std::to_string((int)app().idleMin) + ",";
    out += "\"hiddenKeys\":[";
    {
        bool f = true;
        for (uint8_t k : app().hiddenKeys) { if (!f) out += ","; f = false; out += std::to_string((int)k); }
    }
    out += "],\"excludeApps\":[";
    {
        bool f = true;
        for (auto& e : app().excludeApps) { if (!f) out += ","; f = false; out += "\"" + jsonEscape(e.c_str()) + "\""; }
    }
    out += "]},\"days\":[";
    bool firstDay = true;
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        const DayData& d = kv.second;
        if (!firstDay) out += ",";
        firstDay = false;
        std::string ds = dayIndexToStr(idx);
        out += "{\"date\":\"" + ds + "\",";
        out += "\"keys\":" + std::to_string(d.keys) + ",";
        out += "\"clicks\":" + std::to_string(d.clicks) + ",";
        out += "\"mLeft\":" + std::to_string(d.mLeft) + ",";
        out += "\"mMid\":" + std::to_string(d.mMid) + ",";
        out += "\"mRight\":" + std::to_string(d.mRight) + ",";
        out += "\"activeSec\":" + std::to_string(d.activeSec) + ",";
        out += "\"idleSec\":" + std::to_string(d.idleSec) + ",";
        out += "\"motion\":" + std::to_string(d.motion) + ",";
        out += "\"distPx\":" + std::to_string(d.distPx) + ",";
        out += "\"distCm\":" + std::to_string(distToCm(d.distPx)) + ",";
        out += "\"maxSessionSec\":" + std::to_string(d.maxSessionSec) + ",";
        out += "\"sessionCount\":" + std::to_string(d.sessionCount) + ",";
        out += "\"hourlyKeys\":[";
        for (int i = 0; i < 24; ++i) { if (i) out += ","; out += std::to_string(d.hourlyKeys[i]); }
        out += "],\"hourlyClicks\":[";
        for (int i = 0; i < 24; ++i) { if (i) out += ","; out += std::to_string(d.hourlyClicks[i]); }
        out += "],\"keyCounts\":{";
        {
            bool f = true;
            for (auto& kc : d.keyCounts) { if (!f) out += ","; f = false; out += "\"" + std::to_string((int)kc.first) + "\":" + std::to_string(kc.second); }
        }
        out += "},\"keyHourly\":{";
        {
            bool firstH = true, groupOpen = false, firstK = true;
            int curH = -1;
            for (auto& kh : d.keyHourly) {
                int h = (int)(kh.first >> 8);
                if (h != curH) {
                    if (groupOpen) out += "}";
                    if (!firstH) out += ",";
                    firstH = false;
                    out += "\"" + std::to_string(h) + "\":{";
                    groupOpen = true;
                    curH = h;
                    firstK = true;
                }
                if (!firstK) out += ",";
                firstK = false;
                out += "\"" + std::to_string((int)(kh.first & 0xFF)) + "\":" + std::to_string(kh.second);
            }
            if (groupOpen) out += "}";
        }
        out += "},\"mouseHeat\":{";
        {
            bool f = true;
            for (auto& mh : d.mouseHeat) { if (!f) out += ","; f = false; out += "\"" + std::to_string(mh.first) + "\":" + std::to_string(mh.second); }
        }
        out += "},\"appCounts\":{";
        {
            bool f = true;
            for (auto& ac : d.appCounts) { if (!f) out += ","; f = false; out += "\"" + jsonEscape(ac.first.c_str()) + "\":" + std::to_string(ac.second); }
        }
        out += "},\"minuteActivity\":{";
        {
            bool f = true;
            for (auto& m : d.minuteActivity) { if (!f) out += ","; f = false; out += "\"" + std::to_string(m.first) + "\":" + std::to_string(m.second); }
        }
        out += "},\"keyMinuteActivity\":{";
        {
            bool f = true;
            for (auto& m : d.keyMinuteActivity) { if (!f) out += ","; f = false; out += "\"" + std::to_string(m.first) + "\":" + std::to_string(m.second); }
        }
        out += "},\"clickMinuteActivity\":{";
        {
            bool f = true;
            for (auto& m : d.clickMinuteActivity) { if (!f) out += ","; f = false; out += "\"" + std::to_string(m.first) + "\":" + std::to_string(m.second); }
        }
        out += "}}";
    }
    out += "]}";
    std::vector<unsigned char> bytes(out.begin(), out.end());
    return writeFileBytes(path, bytes);
}

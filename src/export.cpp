// CSV / JSON 导出：纯文本拼接，零第三方依赖。
// 结构：开头数据总览，下方仅保留分日期 → 分应用 → 分分钟 → 分按键 的原始明细（按键/里程/活跃）。
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

// 范围内数据总览统计
struct ExportSummary {
    uint64_t days = 0, apps = 0;
    uint64_t keys = 0, clicks = 0, motion = 0, activeSec = 0, distPx = 0;
};
static ExportSummary buildSummary(int startIdx, int endIdx) {
    ExportSummary s;
    std::set<std::string> allApps;
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        ++s.days;
        const DayData& d = kv.second;
        s.keys += d.keys; s.clicks += d.clicks; s.motion += d.motion;
        s.activeSec += d.activeSec; s.distPx += d.distPx;
        for (auto& ap : d.appMin) allApps.insert(ap.first);
    }
    s.apps = allApps.size();
    return s;
}

// 收集某应用在某天的全部活跃分钟（各子 map 分钟并集）
static void collectAppMinutes(const AppMinuteData& am, std::set<uint16_t>& mins) {
    for (auto& mm : am.keyByMinute) mins.insert(mm.first);
    for (auto& mm : am.clickByMinute) mins.insert(mm.first);
    for (auto& mm : am.motionByMinute) mins.insert(mm.first);
    for (auto& mm : am.movePxByMinute) mins.insert(mm.first);
}

bool ExportCSV(const std::wstring& path, int startIdx, int endIdx) {
    std::string out;
    out += "\xEF\xBB\xBF"; // UTF-8 BOM
    out += "# KeyMouseTracker 数据导出\r\n";
    out += "# 导出时间: " + nowStr() + "\r\n";
    out += "# 数据范围: " + dayIndexToStr(startIdx) + " ~ " + dayIndexToStr(endIdx) + "\r\n\r\n";

    // 1) 数据总览
    ExportSummary sum = buildSummary(startIdx, endIdx);
    out += "=== 数据总览 ===\r\n";
    out += "记录天数,应用数,总按键,总点击,总移动采样,总移动像素,总活跃秒,总位移厘米\r\n";
    out += std::to_string(sum.days) + "," + std::to_string(sum.apps) + ",";
    out += std::to_string(sum.keys) + "," + std::to_string(sum.clicks) + ",";
    out += std::to_string(sum.motion) + "," + std::to_string(sum.distPx) + ",";
    out += std::to_string(sum.activeSec) + "," + std::to_string(distToCm(sum.distPx)) + "\r\n\r\n";

    // 2) 分应用分钟按键明细（仅原始数据）
    out += "=== 分应用分钟按键明细 ===\r\n";
    out += "日期,应用,时间,键码,键名,按键次数,移动像素\r\n";
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        const DayData& d = kv.second;
        std::string ds = dayIndexToStr(idx);
        for (auto& ap : d.appMin) {
            const AppMinuteData& am = ap.second;
            std::set<uint16_t> mins;
            collectAppMinutes(am, mins);
            for (uint16_t m : mins) {
                if (m > 1439) continue;
                auto itp = am.movePxByMinute.find(m);
                uint32_t movePx = (itp != am.movePxByMinute.end()) ? itp->second : 0;
                auto itk = am.keyByMinute.find(m);
                if (itk != am.keyByMinute.end() && !itk->second.empty()) {
                    for (auto& kv2 : itk->second) {
                        char kb[32];
                        out += ds + "," + csvEscape(ap.first) + "," + minToHHMM(m) + ",";
                        out += std::to_string((int)kv2.first) + "," + csvEscape(vkLabel(kv2.first, kb)) + ",";
                        out += std::to_string(kv2.second) + "," + std::to_string(movePx) + "\r\n";
                    }
                } else {
                    out += ds + "," + csvEscape(ap.first) + "," + minToHHMM(m) + ",,,";
                    out += "," + std::to_string(movePx) + "\r\n";
                }
            }
        }
    }

    std::vector<unsigned char> bytes(out.begin(), out.end());
    return writeFileBytes(path, bytes);
}

bool ExportJSON(const std::wstring& path, int startIdx, int endIdx) {
    std::string out;
    auto indent = [&](int n) { for (int i = 0; i < n; ++i) out += "  "; };

    ExportSummary sum = buildSummary(startIdx, endIdx);

    out += "{\n";
    indent(1); out += "\"app\": \"KeyMouseTracker\",\n";
    indent(1); out += "\"exportTime\": \"" + nowStr() + "\",\n";
    indent(1); out += "\"range\": { \"start\": \"" + dayIndexToStr(startIdx) + "\", \"end\": \"" + dayIndexToStr(endIdx) + "\" },\n";
    indent(1); out += "\"summary\": {\n";
    indent(2); out += "\"days\": " + std::to_string(sum.days) + ",\n";
    indent(2); out += "\"apps\": " + std::to_string(sum.apps) + ",\n";
    indent(2); out += "\"totalKeys\": " + std::to_string(sum.keys) + ",\n";
    indent(2); out += "\"totalClicks\": " + std::to_string(sum.clicks) + ",\n";
    indent(2); out += "\"totalMotion\": " + std::to_string(sum.motion) + ",\n";
    indent(2); out += "\"totalActiveSec\": " + std::to_string(sum.activeSec) + ",\n";
    indent(2); out += "\"totalDistCm\": " + std::to_string(distToCm(sum.distPx)) + "\n";
    indent(1); out += "},\n";
    indent(1); out += "\"days\": [\n";

    bool firstDay = true;
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        const DayData& d = kv.second;
        if (!firstDay) out += ",\n";
        firstDay = false;
        indent(2); out += "{\n";
        indent(3); out += "\"date\": \"" + dayIndexToStr(idx) + "\",\n";
        indent(3); out += "\"apps\": [\n";
        bool firstApp = true;
        for (auto& ap : d.appMin) {
            const AppMinuteData& am = ap.second;
            std::set<uint16_t> mins;
            collectAppMinutes(am, mins);
            if (!firstApp) out += ",\n";
            firstApp = false;
            indent(4); out += "{\n";
            indent(5); out += "\"name\": \"" + jsonEscape(ap.first.c_str()) + "\",\n";
            indent(5); out += "\"minutes\": [\n";
            bool firstMin = true;
            for (uint16_t m : mins) {
                if (m > 1439) continue;
                if (!firstMin) out += ",\n";
                firstMin = false;
                indent(6); out += "{ ";
                out += "\"minute\": " + std::to_string(m) + ", ";
                out += "\"time\": \"" + minToHHMM(m) + "\", ";
                auto itk = am.keyByMinute.find(m);
                out += "\"keys\": ";
                if (itk != am.keyByMinute.end() && !itk->second.empty()) {
                    out += "{ ";
                    bool fk = true;
                    for (auto& kv2 : itk->second) {
                        if (!fk) out += ", "; fk = false;
                        out += "\"" + std::to_string((int)kv2.first) + "\": " + std::to_string(kv2.second);
                    }
                    out += " }";
                } else {
                    out += "{}";
                }
                auto itp = am.movePxByMinute.find(m);
                out += ", \"movePx\": " + std::to_string(itp != am.movePxByMinute.end() ? itp->second : 0);
                out += ", \"active\": true }";
            }
            out += "\n";
            indent(5); out += "]\n";
            indent(4); out += "}";
        }
        out += "\n";
        indent(3); out += "]\n";
        indent(2); out += "}";
    }
    out += "\n";
    indent(1); out += "]\n";
    out += "}\n";

    std::vector<unsigned char> bytes(out.begin(), out.end());
    return writeFileBytes(path, bytes);
}

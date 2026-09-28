#pragma once
// 无全局依赖的纯工具函数（JSON 数值/字符串提取、宽松日期解析、系列标志解析等）。
// 从 main.cpp 抽离，供 UI 层复用并便于单测。
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include "data.h"   // daysInMonth / dayIndexToStr

// 从 JSON 片段提取首个整数值（找不到返回 fallback）
inline int jsonInt(const char* json, int fallback) {
    if (!json) return fallback;
    while (*json && !((*json >= '0' && *json <= '9') || *json == '-')) ++json;
    if (!*json) return fallback;
    return (int)strtod(json, nullptr);
}

// 从 JSON 片段提取首个双引号字符串值（去转义；为空/找不到返回 fallback）
inline std::string jsonText(const char* json, const char* fallback) {
    if (!json) return fallback;
    const char* p = strchr(json, '"');
    if (!p) return fallback;
    ++p;
    std::string out;
    while (*p && *p != '"') { if (*p != '\\') out.push_back(*p); ++p; }
    return out.empty() ? fallback : out;
}

// 宽松日期解析：提取字符串中的数字，支持 2026-08-30 / 20260830 等写法（need=8 需 8 位数字）。
// need=8 取年月日，need=6 取年月。解析失败返回 false。
inline bool parseYMDLoose(const std::string& s, int need, int& y, int& m, int& d) {
    std::string digits;
    for (char c : s) if (c >= '0' && c <= '9') digits.push_back(c);
    if ((int)digits.size() < need) return false;
    y = atoi(digits.substr(0, 4).c_str());
    m = atoi(digits.substr(4, 2).c_str());
    d = (need >= 8) ? atoi(digits.substr(6, 2).c_str()) : 1;
    return y >= 2020 && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, m);
}

// 从「YYYY-MM-DD HH:MM」中提取小时/分钟（找不到返回 0:0）
inline void parseDetailHHMM(const std::string& s, int& h, int& n) {
    h = n = 0;
    size_t sp = s.find(' ');
    if (sp == std::string::npos) return;
    std::string digits;
    for (size_t i = sp + 1; i < s.size(); ++i)
        if (s[i] >= '0' && s[i] <= '9') digits.push_back(s[i]);
    if (digits.size() >= 4) {
        h = atoi(digits.substr(0, 2).c_str());
        n = atoi(digits.substr(2, 2).c_str());
        if (h < 0 || h > 23) h = 0;
        if (n < 0 || n > 59) n = 0;
    }
}

// 日序号 → 年月日（逆运算 dayIndexFromYMD）
inline void ymdFromDayIndex(int idx, int& y, int& m, int& d) {
    std::string ds = dayIndexToStr(idx);
    y = m = d = 0;
    if (ds.size() >= 10) {
        y = atoi(ds.substr(0, 4).c_str());
        m = atoi(ds.substr(5, 2).c_str());
        d = atoi(ds.substr(8, 2).c_str());
    }
}

// 解析系列勾选 JSON（如 "[1,0,1,0]"）→ 勾选系列下标列表（0..3）
inline std::vector<int> parseSeriesFlags(const char* j) {
    std::vector<int> flags, idx;
    const char* p = strchr(j, '[');
    if (p) {
        ++p;
        while (*p && *p != ']') {
            while (*p && (*p == ' ' || *p == ',' || *p == '\t')) ++p;
            if (*p == ']' || !*p) break;
            char* end = nullptr;
            long v = strtol(p, &end, 10);
            if (end == p) break;
            flags.push_back((int)v);
            p = end;
        }
    }
    for (size_t i = 0; i < flags.size() && i < 4; ++i)
        if (flags[i]) idx.push_back((int)i);
    return idx;
}

// UTF-8 → UTF-16（ASCII 原样通过，中文等正确解码）。UI 侧显示中文标签用。
inline std::wstring widen(const std::string& s) {
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

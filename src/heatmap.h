#pragma once
// 热力图渲染工具（自 main.cpp 抽离）：键盘配列布局（108/87/61）+ 热力色阶/归一化。
// 纯几何与颜色计算，无交互状态，供热力图绘制、命中测试与趋势矩阵热力图复用。
#include <ui_core.h>
#include <data.h>
#include "uicolor.h"
#include <vector>
#include <cmath>

struct KeyCell { const wchar_t* l; uint8_t vk; uint8_t zone; float x, y, w, h; };

// 主键区宽 15u，编辑键区宽 3u，小键盘区宽 4u
inline const float kZoneUnits[3] = { 15.0f, 3.0f, 4.0f };
inline const float kBoardMaxY = 6.0f;

// 分区起点像素偏移：分区间距 = 2×普通键间距（gap 为普通键之间的视觉缝隙）
inline float zoneOffset(int zone, float unit, float gap) {
    float off = 0;
    for (int i = 0; i < zone; ++i) off += kZoneUnits[i] * unit + 2.0f * gap;
    return off;
}

// ---- 键盘配列（关于页设置，持久化）----
// 0=108 全尺寸（三区全显） 1=87 TKL（去小键盘） 2=61 紧凑（仅主键区，
// 去 F1~F12 与 `，Esc 下移占据 ` 位 —— 与真实 60% 配列一致）
inline int layoutZoneCount() { return app().kbLayout == 0 ? 3 : (app().kbLayout == 1 ? 2 : 1); }
inline float layoutBoardMaxY() {
    // 61 键：去掉 F 行后内容只有 5 行（数字行 … 底行，坐标整体上移一行 0..4），
    // 板高用 5 才能让整块内容在画布内垂直居中；沿用 6 会多留一行空档偏上。
    return app().kbLayout == 2 ? 5.0f : kBoardMaxY;
}
inline float layoutZoneUnitsSum() {
    float s = 0;
    for (int i = 0; i < layoutZoneCount(); ++i) s += kZoneUnits[i];
    return s;
}
inline bool keyInLayout(const KeyCell& k) {
    if (app().kbLayout == 0) return true;
    if (k.zone >= layoutZoneCount()) return false;   // 87/61：无小键盘；61：无编辑区
    if (app().kbLayout == 2) {
        if (k.vk >= 112 && k.vk <= 123) return false;
        if (k.vk == 192) return false;
    }
    return true;
}

// 标准全尺寸键盘布局（108 键）。主键区各行总宽 15u 严格对齐。
inline const KeyCell kKeys[] = {
    { L"Esc",  27, 0,  0.0f, 0, 1, 1 },
    { L"F1",  112, 0,  2.0f, 0, 1, 1 }, { L"F2", 113, 0,  3.0f, 0, 1, 1 },
    { L"F3",  114, 0,  4.0f, 0, 1, 1 }, { L"F4", 115, 0,  5.0f, 0, 1, 1 },
    { L"F5",  116, 0,  6.5f, 0, 1, 1 }, { L"F6", 117, 0,  7.5f, 0, 1, 1 },
    { L"F7",  118, 0,  8.5f, 0, 1, 1 }, { L"F8", 119, 0,  9.5f, 0, 1, 1 },
    { L"F9",  120, 0, 11.0f, 0, 1, 1 }, { L"F10", 121, 0, 12.0f, 0, 1, 1 },
    { L"F11", 122, 0, 13.0f, 0, 1, 1 }, { L"F12", 123, 0, 14.0f, 0, 1, 1 },

    { L"`", 192, 0, 0, 1, 1, 1 },
    { L"1", 49, 0, 1, 1, 1, 1 }, { L"2", 50, 0, 2, 1, 1, 1 }, { L"3", 51, 0, 3, 1, 1, 1 },
    { L"4", 52, 0, 4, 1, 1, 1 }, { L"5", 53, 0, 5, 1, 1, 1 }, { L"6", 54, 0, 6, 1, 1, 1 },
    { L"7", 55, 0, 7, 1, 1, 1 }, { L"8", 56, 0, 8, 1, 1, 1 }, { L"9", 57, 0, 9, 1, 1, 1 },
    { L"0", 48, 0, 10, 1, 1, 1 },
    { L"-", 189, 0, 11, 1, 1, 1 }, { L"=", 187, 0, 12, 1, 1, 1 },
    { L"Back", 8, 0, 13, 1, 2, 1 },

    { L"Tab", 9, 0, 0, 2, 1.5f, 1 },
    { L"Q", 81, 0, 1.5f, 2, 1, 1 }, { L"W", 87, 0, 2.5f, 2, 1, 1 }, { L"E", 69, 0, 3.5f, 2, 1, 1 },
    { L"R", 82, 0, 4.5f, 2, 1, 1 }, { L"T", 84, 0, 5.5f, 2, 1, 1 }, { L"Y", 89, 0, 6.5f, 2, 1, 1 },
    { L"U", 85, 0, 7.5f, 2, 1, 1 }, { L"I", 73, 0, 8.5f, 2, 1, 1 }, { L"O", 79, 0, 9.5f, 2, 1, 1 },
    { L"P", 80, 0, 10.5f, 2, 1, 1 },
    { L"[", 219, 0, 11.5f, 2, 1, 1 }, { L"]", 221, 0, 12.5f, 2, 1, 1 }, { L"\\", 220, 0, 13.5f, 2, 1.5f, 1 },

    { L"Caps", 20, 0, 0, 3, 1.75f, 1 },
    { L"A", 65, 0, 1.75f, 3, 1, 1 }, { L"S", 83, 0, 2.75f, 3, 1, 1 }, { L"D", 68, 0, 3.75f, 3, 1, 1 },
    { L"F", 70, 0, 4.75f, 3, 1, 1 }, { L"G", 71, 0, 5.75f, 3, 1, 1 }, { L"H", 72, 0, 6.75f, 3, 1, 1 },
    { L"J", 74, 0, 7.75f, 3, 1, 1 }, { L"K", 75, 0, 8.75f, 3, 1, 1 }, { L"L", 76, 0, 9.75f, 3, 1, 1 },
    { L";", 186, 0, 10.75f, 3, 1, 1 }, { L"'", 222, 0, 11.75f, 3, 1, 1 },
    { L"Enter", 13, 0, 12.75f, 3, 2.25f, 1 },

    { L"Shift", 160, 0, 0, 4, 2.25f, 1 },
    { L"Z", 90, 0, 2.25f, 4, 1, 1 }, { L"X", 88, 0, 3.25f, 4, 1, 1 }, { L"C", 67, 0, 4.25f, 4, 1, 1 },
    { L"V", 86, 0, 5.25f, 4, 1, 1 }, { L"B", 66, 0, 6.25f, 4, 1, 1 }, { L"N", 78, 0, 7.25f, 4, 1, 1 },
    { L"M", 77, 0, 8.25f, 4, 1, 1 },
    { L",", 188, 0, 9.25f, 4, 1, 1 }, { L".", 190, 0, 10.25f, 4, 1, 1 }, { L"/", 191, 0, 11.25f, 4, 1, 1 },
    { L"Shift", 161, 0, 12.25f, 4, 2.75f, 1 },

    { L"Ctrl", 162, 0, 0, 5, 1.25f, 1 },
    { L"Win", 91, 0, 1.25f, 5, 1.25f, 1 },
    { L"Alt", 164, 0, 2.5f, 5, 1.25f, 1 },
    { L"", 32, 0, 3.75f, 5, 6.25f, 1 },
    { L"Alt", 165, 0, 10.0f, 5, 1.25f, 1 },
    { L"Win", 92, 0, 11.25f, 5, 1.25f, 1 },
    { L"Menu", 93, 0, 12.5f, 5, 1.25f, 1 },
    { L"Ctrl", 163, 0, 13.75f, 5, 1.25f, 1 }, // 右 Ctrl

    { L"PrtSc", 44, 1, 0, 0, 1, 1 }, { L"ScrLk", 145, 1, 1, 0, 1, 1 }, { L"Pause", 19, 1, 2, 0, 1, 1 },
    { L"Ins", 45, 1, 0, 1, 1, 1 },  { L"Home", 36, 1, 1, 1, 1, 1 },  { L"PgUp", 33, 1, 2, 1, 1, 1 },
    { L"Del", 46, 1, 0, 2, 1, 1 },  { L"End", 35, 1, 1, 2, 1, 1 },   { L"PgDn", 34, 1, 2, 2, 1, 1 },
    { L"\u2191", 38, 1, 1, 4, 1, 1 },
    { L"\u2190", 37, 1, 0, 5, 1, 1 }, { L"\u2193", 40, 1, 1, 5, 1, 1 }, { L"\u2192", 39, 1, 2, 5, 1, 1 },

    { L"Num", 144, 2, 0, 1, 1, 1 },
    { L"/", 111, 2, 1, 1, 1, 1 },
    { L"*", 106, 2, 2, 1, 1, 1 },
    { L"-", 109, 2, 3, 1, 1, 1 },
    { L"7", 103, 2, 0, 2, 1, 1 }, { L"8", 104, 2, 1, 2, 1, 1 }, { L"9", 105, 2, 2, 2, 1, 1 },
    { L"+", 107, 2, 3, 2, 1, 2 },
    { L"4", 100, 2, 0, 3, 1, 1 }, { L"5", 101, 2, 1, 3, 1, 1 }, { L"6", 102, 2, 2, 3, 1, 1 },
    { L"1", 97, 2, 0, 4, 1, 1 }, { L"2", 98, 2, 1, 4, 1, 1 }, { L"3", 99, 2, 2, 4, 1, 1 },
    { L"Enter", 13, 2, 3, 4, 1, 2 },
    { L"0", 96, 2, 0, 5, 2, 1 },
    { L".", 110, 2, 2, 5, 1, 1 },
};

inline const int kNumKeys = (int)(sizeof(kKeys) / sizeof(KeyCell));

// vk 可能在布局表中多次出现（如主键区/小键盘区各有一个 Enter）——任一处
// 可见即参与当前配列的色阶归一化
inline bool vkInLayout(uint8_t vk) {
    for (int i = 0; i < kNumKeys; ++i)
        if (kKeys[i].vk == vk && keyInLayout(kKeys[i])) return true;
    return false;
}

// 8 级热力色阶（t∈[0,1] 量化到 8 档，由冷到暖）
// 暗色模式整体压暗（各通道 ×0.72）：同样的色相在深色背景上视觉亮度更低，
// 避免高亮档（黄/橙）在暗色下刺眼。
inline UiColor heatColor(float t, bool dark) {
    static const int stops[8][3] = {
        { 63, 120, 244 },  { 56, 190, 242 },  { 84, 214, 196 },  { 118, 226, 116 },
        { 186, 222, 74 },  { 249, 205, 66 },  { 245, 148, 66 },  { 236, 88, 78 }
    };
    if (t <= 0) t = 0; if (t > 1) t = 1;
    int i = (int)(t * 7.999f);
    float k = dark ? 0.72f : 1.0f;
    return rgb255((int)(stops[i][0] * k), (int)(stops[i][1] * k), (int)(stops[i][2] * k));
}

// 热力图归一化：在「当前可见非零样本的最小→最大」之间做 log 插值，使可见键/格总是
// 铺满 8 档色阶；隐藏任意键后 min/max 重算，热度立即重新排序、低档颜色随之出现。
inline void heatVisibleRange(const std::vector<uint32_t>& vals, uint32_t& mn, uint32_t& mx) {
    if (vals.empty()) { mn = 1; mx = 1; return; }
    mn = vals[0]; mx = vals[0];
    for (size_t i = 1; i < vals.size(); ++i) {
        if (vals[i] > mx) mx = vals[i];
        if (vals[i] < mn) mn = vals[i];
    }
    if (mx == 0) mx = 1;
    if (mn == 0) mn = 1;
}

// c 在 [mn,mx] 之间做 log 插值：越靠 mn 越冷（低档），越靠 mx 越热（高档）。
inline float heatT(uint32_t c, uint32_t mn, uint32_t mx) {
    if (c == 0) return 0.0f;
    if (mx <= mn) return 1.0f;   // 可见样本全相等：取最高档
    double lc = std::log1p((double)(c > mx ? mx : c));
    double lmin = std::log1p((double)mn);
    double lmax = std::log1p((double)mx);
    double t = (lc - lmin) / (lmax - lmin);
    if (t < 0) t = 0; if (t > 1) t = 1;
    return (float)t;
}

// 统一自绘时间序列图表组件（供 键盘统计表 / 鼠标统计表 / 历史趋势 三处复用）
// 纯逻辑 + core-ui ui_draw_*，不引入第三方图表库。核心算法参考 research-notes.md：
// - 视口跨度 → 自适应聚合桶宽（精度阶梯）
// - 滚轮以光标为锚点缩放、拖拽平移
// - 纵轴随可见区间联动重定标 + 左右双轴
// - 十字光标吸附 + 按精度的悬浮浮窗
// - 档位/纵轴变化 60Hz 平滑插值（main 侧每帧 tick）
#pragma once
#include <ui_core.h>
#include <data.h>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <algorithm>

// UTF-8 → UTF-16（组件内自用，避免依赖 main.cpp 的 static widen）
inline std::wstring tsWiden(const std::string& s) {
    std::wstring w; w.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0; int len = 0;
        if ((c & 0x80) == 0)          { cp = c;        len = 1; }
        else if ((c & 0xE0) == 0xC0)  { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07; len = 4; }
        else                          { w.push_back((wchar_t)c); i++; continue; }
        if (i + len > s.size()) break;
        for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += len;
        if (cp < 0x10000) w.push_back((wchar_t)cp);
        else { cp -= 0x10000; w.push_back((wchar_t)(0xD800 | (cp >> 10))); w.push_back((wchar_t)(0xDC00 | (cp & 0x3FF))); }
    }
    return w;
}

// 数值缩写（纵轴/浮窗共用）
inline std::wstring tsFmtNum(double v) {
    wchar_t b[32];
    if (v < 0) v = 0;
    if (v < 1000)        { _snwprintf_s(b, 32, L"%.0f", v); }
    else if (v < 1000000){ _snwprintf_s(b, 32, L"%.1fk", v / 1000.0); }
    else if (v < 1000000000ULL) { _snwprintf_s(b, 32, L"%.1fM", v / 1000000.0); }
    else                 { _snwprintf_s(b, 32, L"%.2fB", v / 1000000000.0); }
    return b;
}

// 桶值系列（一次取数返回多系列，供叠加）
struct TSSeries {
    std::vector<float> v;      // 每桶值（长度 = 可见桶数）
    UiColor color;             // 绘制色
    std::wstring name;         // 图例/浮窗名
    int axis = 0;              // 0=左轴 1=右轴
};

// 取数回调：填充 [firstBucket, firstBucket+count) 桶，桶宽 bw（分钟）
typedef void (*TSFillFn)(int bw, int64_t firstBucket, int count,
                         std::vector<TSSeries>& out, void* ud);

// 单个图表实例（全局每个 site 一份）。坐标：全部使用"全局分钟" = 日序号*1440 + 当日分钟。
struct TimeSeriesChart {
    // ---- 视图状态（显示值，动画逼近 target）----
    double tStart = 0;        // 窗口起点（全局分钟）
    double span = 1440;       // 窗口跨度（分钟）
    double targetT = 0, targetSpan = 1440;
    bool   animating = false;
    // ---- 拖拽平移 ----
    bool   dragging = false;
    float  dragX = 0;
    double dragStartT = 0;
    // ---- 十字光标/悬浮 ----
    bool   hoverActive = false;
    float  hoverX = 0, hoverY = 0;
    int    hoverBucket = -1;  // 相对本次可见桶起点的下标（-1=未命中）
    // ---- 取数/数据范围 ----
    TSFillFn fill = nullptr;
    void*    ud = nullptr;
    double   dataMin = 0, dataMax = 1;   // 数据覆盖的全局分钟范围（钳制窗口用）
    std::vector<int> enabled;            // 取数结果中要保留的系列下标（用于屏蔽不需要的系列）
    // ---- 渲染样式 ----
    bool   line = false;                 // true=折线（默认柱状）
    float  maxVis[2] = { 1.0f, 1.0f };   // 左右轴当前显示上限（动画逼近 target）
    // ---- 聚合缓存（平移时跨帧复用桶；缩放跨精度才重算）----
    int    cacheBw = 0; int64_t cacheFirst = 0; bool cacheValid = false;
    std::vector<TSSeries> cache;
    // ---- 绘图区几何（像素，widget-local；由 draw 写入，move/wheel 复用）----
    float plotL = 40, plotR = 14, plotT = 20, plotB = 26;
    float plotX0 = 0, plotX1 = 0, plotY0 = 0, plotY1 = 0;
    int64_t visFirst = 0; int visCount = 0;
    std::vector<TSSeries> curVis;        // 本次可见序列（浮窗/求 max 用）
    int    curBw = 1;

    static constexpr double kMinSpan = 10;              // 最小跨度（分钟）
    static constexpr double kMaxSpan = 365.0 * 1440 * 100;

    // ---- 精度阶梯：视口跨度(分钟) → 聚合桶宽(分钟) ----
    static int bucketForSpan(double spanMin) {
        if (spanMin <= 30)     return 1;       // 10~30min   -> 1min
        if (spanMin <= 150)    return 5;       // 30~150min  -> 5min
        if (spanMin <= 300)    return 10;      // ~5h
        if (spanMin <= 720)    return 30;      // 5~12h
        if (spanMin <= 4320)   return 60;      // 12h~3d -> 1h
        if (spanMin <= 43200)  return 1440;    // 3~30d -> 1d
        if (spanMin <= 129600) return 10080;   // 30~90d -> 1w
        return 43200;                          // >=90d -> 1mo
    }

    void setDataRange(double mn, double mx) {
        if (mx > mn) { dataMin = mn; dataMax = mx; }
    }

    // 设置目标窗口（滚轮/拖拽/切档统一入口），并平滑过渡
    void setTarget(double t, double s) {
        if (s < kMinSpan) s = kMinSpan;
        if (s > kMaxSpan) s = kMaxSpan;
        double lo = dataMin, hi = dataMax;
        if (hi <= lo) { hi = t + s + kMinSpan; lo = t - kMinSpan; }
        if (t < lo) t = lo;
        if (t + s > hi + 1) t = hi - s + 1;
        if (t < lo) t = lo;
        targetT = t; targetSpan = s; animating = true;
    }

    // 滚轮以光标为锚点缩放：vMin = 光标处的时间值，factor = 新/旧跨度比
    void zoomAt(double vMin, double factor) {
        double ns = span * factor;
        if (ns < kMinSpan) ns = kMinSpan;
        if (ns > kMaxSpan) ns = kMaxSpan;
        if (ns == span) return;
        double f = ns / span;
        double nt = vMin - (vMin - tStart) * f;
        setTarget(nt, ns);
    }

    void onDown(float x) { dragging = true; dragX = x; dragStartT = tStart; }
    void onUp() { dragging = false; }
    void onDrag(float x, bool inside) {
        if (!dragging) return;
        double dw = plotX1 - plotX0;
        if (dw <= 0) return;
        double nt = dragStartT - (double)(x - dragX) / dw * span;
        setTarget(nt, span);
        if (!inside) dragging = false;
    }
    // 悬停更新：返回 true 表示悬停状态发生变化（调用方应触发重绘）
    bool onMoveInside(float x, float y) {
        hoverX = x; hoverY = y;
        int b = -1;
        if (visCount > 0 && plotX1 > plotX0 &&
            x >= plotX0 && x <= plotX1 && y >= plotY0 && y <= plotY1) {
            float px = (x - plotX0) / (plotX1 - plotX0);
            int idx = (int)(px * visCount);
            if (idx < 0) idx = 0; if (idx >= visCount) idx = visCount - 1;
            b = idx;
        }
        if (b != hoverBucket) { hoverBucket = b; hoverActive = (b >= 0); return true; }
        return false;
    }
    void onLeave() { hoverBucket = -1; hoverActive = false; }

    // 每帧动画推进；返回 true 表示仍在动画（调用方应安排重绘）
    bool tick(double k = 0.3) {
        if (!animating) return false;
        double nt = tStart + (targetT - tStart) * k;
        double ns = span + (targetSpan - span) * k;
        tStart = nt; span = ns;
        if (std::fabs(targetT - tStart) < 0.5 && std::fabs(targetSpan - span) < 0.5) {
            tStart = targetT; span = targetSpan; animating = false;
            return false;
        }
        return true;
    }

    // 全局分钟 -> 悬浮时间戳（按精度）：分钟级 YYYY-MM-DD HH:MM / 日级 YYYY-MM-DD / 月级 YYYY-MM
    std::wstring labelFor(int bw, int64_t gm) const {
        int day = (int)(gm / 1440); int min = (int)(gm % 1440);
        std::wstring wds = tsWiden(dayIndexToStr(day));
        if (bw >= 1440) {
            if (bw >= 10080 && wds.size() >= 7) return wds.substr(0, 7); // 周/月 -> YYYY-MM
            return wds;                                                  // 日 -> YYYY-MM-DD
        }
        wchar_t buf[40];
        _snwprintf_s(buf, 40, L"%s %02d:%02d", wds.c_str(), min / 60, min % 60);
        return buf;
    }

    // 按可见范围取数（命中缓存直接切片，跨精度才重算）
    void ensureData(int bw, int64_t first, int cnt, bool dark) {
        if (cnt <= 0) cnt = 1;
        bool reuse = cacheValid && !cache.empty() && cacheBw == bw &&
                     first >= cacheFirst && first + cnt <= cacheFirst + (int)cache[0].v.size();
        if (!reuse) {
            std::vector<TSSeries> full;
            if (fill) fill(bw, first, cnt, full, ud);
            cache.swap(full); cacheBw = bw; cacheFirst = first; cacheValid = true;
        }
        curVis.clear();
        int off = (int)(first - cacheFirst);
        for (size_t i = 0; i < cache.size(); ++i) {
            bool use = enabled.empty() || std::find(enabled.begin(), enabled.end(), (int)i) != enabled.end();
            if (!use) continue;
            TSSeries s;
            s.color = cache[i].color; s.name = cache[i].name; s.axis = cache[i].axis;
            s.v.assign(cache[i].v.begin() + off, cache[i].v.begin() + off + cnt);
            curVis.push_back(s);
        }
        visFirst = first; visCount = cnt; curBw = bw;
        (void)dark;
    }

    void draw(UiDrawCtx ctx, UiRect rect, bool dark);

    static UiColor tsColor(int r, int g, int b, int a = 255) {
        return UiColor{ r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    }
};

// 悬浮框绘制（独立以便后续新增纵向标题行简化）
inline void drawTooltipImpl(TimeSeriesChart& c, UiDrawCtx ctx, UiRect rect,
                            std::vector<TSSeries>& curVis, const std::wstring& title,
                            const std::vector<std::wstring>& lines);

// 绘制实现：背景 + 网格 + 纵轴 + 柱/折线 + 十字光标 + 悬浮浮窗
inline void TimeSeriesChart::draw(UiDrawCtx ctx, UiRect rect, bool dark) {
    UiColor axisCol = dark ? tsColor(120, 126, 136) : tsColor(138, 145, 157);
    UiColor gridCol = dark ? tsColor(52, 57, 66) : tsColor(238, 241, 245);
    UiColor bgCol   = dark ? tsColor(20, 23, 28) : tsColor(247, 249, 252);

    float plotW = (rect.right - rect.left) - plotL - plotR;
    float plotH = (rect.bottom - rect.top) - plotT - plotB;
    if (plotW <= 10 || plotH <= 10) { hoverActive = false; return; }
    float px0 = rect.left + plotL, px1 = rect.right - plotR;
    float py0 = rect.top + plotT,  py1 = rect.bottom - plotB;
    plotX0 = px0; plotX1 = px1; plotY0 = py0; plotY1 = py1;

    int bw = bucketForSpan(span);
    int64_t g0 = (int64_t)std::floor(tStart / (double)bw) * bw;
    int64_t g1 = (int64_t)std::ceil((tStart + span) / (double)bw) * bw;
    int64_t first = g0 / bw; int cnt = (int)((g1 - g0) / bw);
    if (cnt <= 0) cnt = 1;
    if (cnt > 400) cnt = 400;

    ensureData(bw, first, cnt, dark);

    // 各轴可见最大（按 curVis 自身量纲归一化，0..max*1.05）；纵轴随可见区间联动重定标
    float maxV[2] = { 1.0f, 1.0f };
    for (auto& s : curVis) {
        for (size_t i = 0; i < (size_t)cnt && i < s.v.size(); ++i) {
            float v = s.v[i];
            int ax = (s.axis == 1) ? 1 : 0;
            if (v > maxV[ax]) maxV[ax] = v;
        }
    }
    maxV[0] *= 1.05f; maxV[1] *= 1.05f;
    if (maxV[0] < 1.0f) maxV[0] = 1.0f;
    if (maxV[1] < 1.0f) maxV[1] = 1.0f;
    // 纵轴上限平滑过渡：动画期间每帧逼近目标，静止时直接对齐（避免残影）
    float axMax[2];
    if (animating) {
        axMax[0] = maxVis[0] += (maxV[0] - maxVis[0]) * 0.3f;
        axMax[1] = maxVis[1] += (maxV[1] - maxVis[1]) * 0.3f;
    } else {
        axMax[0] = maxVis[0] = maxV[0];
        axMax[1] = maxVis[1] = maxV[1];
    }

    // 背景
    ui_draw_fill_rect(ctx, UiRect{ rect.left, rect.top, rect.right, rect.bottom }, bgCol);

    bool anyData = false;
    for (auto& s : curVis) for (float v : s.v) if (v > 0) { anyData = true; break; }
    if (!anyData) {
        hoverBucket = -1; hoverActive = false;
        ui_draw_text_ex(ctx, L"暂无数据", UiRect{ px0, py0, px1, py0 + 24 }, axisCol, 12, 1, 0);
        return;
    }

    // 横/纵网格 + 纵轴标签（每个轴一条，画在左右两侧）
    for (int g = 0; g <= 3; ++g) {
        float gy = py1 - plotH * g / 3.0f;
        ui_draw_line(ctx, px0, gy, px1, gy, gridCol, 1.0f);
        wchar_t lbs[24];
        _snwprintf_s(lbs, 24, L"%s", tsFmtNum(axMax[0] * g / 3.0f).c_str());
        ui_draw_text_ex(ctx, lbs, UiRect{ rect.left + 2, gy - 5, px0 - 2, gy + 7 }, axisCol, 9, 2, 0);
        if (axMax[1] > 1.0f) {   // 右轴（多次量纲）
            _snwprintf_s(lbs, 24, L"%s", tsFmtNum(axMax[1] * g / 3.0f).c_str());
            ui_draw_text_ex(ctx, lbs, UiRect{ px1 + 2, gy - 5, rect.right, gy + 7 }, axisCol, 9, 0, 0);
        }
    }

    // 每桶按桶宽均分槽位，多系列并排
    float slot = plotW / (float)cnt;
    size_t nSeries = curVis.size();
    if (nSeries == 0) return;
    float barW = slot * 0.8f / (float)nSeries;
    if (barW < 1.0f) barW = 1.0f;

    if (line) {
        // 折线：每系列一条多段线（含零值点）
        for (size_t si = 0; si < nSeries; ++si) {
            TSSeries& s = curVis[si];
            int ax = (s.axis == 1) ? 1 : 0;
            float m = axMax[ax];
            float prevX = 0, prevY = 0; bool pen = false;
            for (int b = 0; b < cnt; ++b) {
                float v = (b < (int)s.v.size()) ? s.v[b] : 0.0f;
                float cx = px0 + slot * b + slot * 0.5f;
                float cy = py1 - plotH * (v / m);
                if (pen) ui_draw_line(ctx, prevX, prevY, cx, cy, s.color, 1.5f);
                pen = true; prevX = cx; prevY = cy;
            }
        }
    } else {
        // 柱状
        for (size_t si = 0; si < nSeries; ++si) {
            TSSeries& s = curVis[si];
            int ax = (s.axis == 1) ? 1 : 0;
            float m = axMax[ax];
            for (int b = 0; b < cnt; ++b) {
                float v = (b < (int)s.v.size()) ? s.v[b] : 0.0f;
                float cx = px0 + slot * b + slot * 0.5f;
                if (v <= 0) continue;
                float hbar = plotH * (v / m);
                float bx0 = cx - slot * 0.4f + (float)si * barW;
                ui_draw_fill_rect(ctx, UiRect{ bx0, py1 - hbar, bx0 + barW, py1 }, s.color);
            }
        }
    }

    // 底部时间轴刻度（自适应抽稀，约 6 个）
    int labelStep = cnt / 6 + 1;
    for (int b = 0; b < cnt; b += labelStep) {
        int64_t gm = (visFirst + b) * (int64_t)bw;
        std::wstring l = labelFor(bw, gm);
        float cx = px0 + slot * b + slot * 0.5f;
        UiRect lr = { cx - slot * labelStep * 0.5f, py1 + 3, cx + slot * labelStep * 0.5f, py1 + 18 };
        ui_draw_text_ex(ctx, l.c_str(), lr, axisCol, 9, 2, 0);
    }

    // 十字光标：吸附最近桶中心 + 高亮 + 浮窗
    if (hoverActive && hoverBucket >= 0 && hoverBucket < cnt) {
        float ccx = px0 + slot * hoverBucket + slot * 0.5f;
        ui_draw_line(ctx, ccx, py0, ccx, py1, tsColor(176, 186, 198, 200), 1.0f);
        if (line) {
            // 折线：各系列在命中桶处画圆点
            for (size_t si = 0; si < nSeries; ++si) {
                TSSeries& s = curVis[si];
                int ax = (s.axis == 1) ? 1 : 0;
                float v = (hoverBucket < (int)s.v.size()) ? s.v[hoverBucket] : 0.0f;
                float m = axMax[ax];
                float cy = py1 - plotH * (v / m);
                ui_draw_fill_rounded_rect(ctx, UiRect{ ccx - 2.5f, cy - 2.5f, ccx + 2.5f, cy + 2.5f },
                                          2.5f, 2.5f, s.color);
            }
        } else {
            // 柱状：高亮该桶各系列柱（覆盖半透明描边）
            for (size_t si = 0; si < nSeries; ++si) {
                TSSeries& s = curVis[si];
                int ax = (s.axis == 1) ? 1 : 0;
                float v = (hoverBucket < (int)s.v.size()) ? s.v[hoverBucket] : 0.0f;
                if (v <= 0) continue;
                float m = axMax[ax];
                float hbar = plotH * (v / m);
                float bx0 = ccx - slot * 0.4f + (float)si * barW;
                ui_draw_rounded_rect(ctx, UiRect{ bx0, py1 - hbar, bx0 + barW, py1 },
                                     0.0f, 0.0f, tsColor(255, 255, 255, 130), 1.0f);
            }
        }
        // 浮窗：第一行按精度的时间戳，后续行各系列精确数值
        int64_t gm0 = (visFirst + hoverBucket) * (int64_t)bw;
        std::wstring title = labelFor(bw, gm0);
        std::vector<std::wstring> lines;
        for (size_t si = 0; si < nSeries; ++si) {
            TSSeries& s = curVis[si];
            float v = (hoverBucket < (int)s.v.size()) ? s.v[hoverBucket] : 0.0f;
            wchar_t vb[40];
            if (v >= 1000000000.0f)   _snwprintf_s(vb, 40, L"%.2fB", v / 1e9f);
            else if (v >= 1000000.0f) _snwprintf_s(vb, 40, L"%.1fM", v / 1e6f);
            else                      _snwprintf_s(vb, 40, L"%.0f", v);
            lines.push_back(s.name + L" " + vb);
        }
        drawTooltipImpl(*this, ctx, rect, curVis, title, lines);
    }
}

// 悬浮框绘制（独立以便后续新增纵向标题行简化）
inline void drawTooltipImpl(TimeSeriesChart& c, UiDrawCtx ctx, UiRect rect,
                            std::vector<TSSeries>& curVis, const std::wstring& title,
                            const std::vector<std::wstring>& lines) {
    float maxW = ui_draw_measure_text(ctx, title.c_str(), 12);
    for (auto& ln : lines) { float w = ui_draw_measure_text(ctx, ln.c_str(), 12); if (w > maxW) maxW = w; }
    float bw2 = maxW + 20;
    float bh = 22.0f + lines.size() * 16.0f;
    float by = c.hoverY - bh - 8; if (by < rect.top) by = c.hoverY + 12;
    float bx = c.hoverX - bw2 - 10; if (bx < rect.left) bx = c.hoverX + 10;
    UiRect br = { bx, by, bx + bw2, by + bh };
    ui_draw_fill_rounded_rect(ctx, br, 5.0f, 5.0f, TimeSeriesChart::tsColor(24, 26, 31, 235));
    ui_draw_rounded_rect(ctx, br, 5.0f, 5.0f, TimeSeriesChart::tsColor(120, 126, 136, 120), 1.0f);
    ui_draw_text_ex(ctx, title.c_str(), UiRect{ br.left + 10, br.top + 2, br.right - 4, br.top + 18 },
                    TimeSeriesChart::tsColor(255, 255, 255), 12, 2, 0);
    for (size_t i = 0; i < lines.size() && i < curVis.size(); ++i) {
        float yy = br.top + 18 + (float)i * 16;
        ui_draw_text_ex(ctx, lines[i].c_str(),
                        UiRect{ br.left + 10, yy, br.right - 4, yy + 16 },
                        curVis[i].color, 12, 2, 0);
    }
}
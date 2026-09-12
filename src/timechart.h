// 统一自绘时间序列图表组件（历史-统计表 / 总览 24h 趋势 复用）
// 纯逻辑 + core-ui ui_draw_*，不引入第三方图表库。设计要点：
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
#include <unordered_map>

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
    int axis = 0;              // 0=L1(左内) 1=L2(左外) 2=R1(右内) 3=R2(右外)；见 D3
    float alpha = 1.0f;        // 系列透明度（勾选渐入/渐出，0..1；D1）
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
    double panFrac = 0.0;     // 窗口起点在桶内的分数偏移（0..1），拖拽时子桶平滑滑入
    double bucketsPerPlot = 1.0;  // 每绘图区宽度的桶数 = span/bw，用于精确横坐标与命中
    // ---- 拖拽平移 ----
    bool   dragging = false;
    float  dragX = 0;
    double dragStartT = 0;
    double flingVel = 0.0;      // 松手后的平移动量（分钟/帧），逐帧摩擦衰减
    double dtMs = 16.0;         // 最近一帧时间间隔(ms)，draw() 内纵轴收敛也按此时间缩放
    // ---- 十字光标/悬浮 ----
    bool   hoverActive = false;
    float  hoverX = 0, hoverY = 0;
    int    hoverBucket = -1;  // 相对本次可见桶起点的下标（-1=未命中）
    // ---- 取数/数据范围 ----
    TSFillFn fill = nullptr;
    void*    ud = nullptr;
    double   dataMin = 0, dataMax = 1;   // 数据覆盖的全局分钟范围（钳制窗口用）
    std::vector<int> enabled;            // 取数结果中要保留的系列下标（用于屏蔽不需要的系列）
    std::vector<float> alpha;            // 每 fill 输出系列当前透明度（勾选渐入/渐出）
    std::vector<float> alphaTarget;      // 目标透明度（由 enabled 决定：1=显示 0=隐藏）
    // ---- 渲染样式 ----
    bool   line = false;                 // true=折线（默认柱状）
    float  maxVis[4] = { 1.0f, 1.0f, 1.0f, 1.0f };  // 4 轴槽上限（L1,L2,R1,R2；动画逼近）
    bool   maxVisInit = false;                     // 首次绘制直接对齐纵轴，之后平滑追赶
    // ---- 聚合缓存（多精度独立缓存：key=桶宽 bw，每精度一份 data+first）----
    // 缩放跨精度直接命中对应精度缓存，无需重算已算过的桶；平移仅在跨边界时扩算。
    struct CacheEntry {
        std::vector<TSSeries> data;
        int64_t first = 0;
        int64_t updatedTick = 0;   // 写入时的逻辑帧号，用于过期淘汰
    };
    std::unordered_map<int, CacheEntry> caches;   // bw -> 该精度的桶数组
    int64_t cacheTick = 0;                        // 单调递增缓存帧号
    std::vector<TSSeries> cache;                  // 最近一次命中的精度（绘制用别名，避免拷贝）
    // ---- 绘图区几何（像素，widget-local；由 draw 写入，move/wheel 复用）----
    // gutter 按 4 轴槽动态计算（每侧 44px/槽固定宽度，避免 maxVis 动画期间文本长度变化造成布局抖动）
    float plotL = 40, plotR = 14, plotT = 20, plotB = 26;
    float plotX0 = 0, plotX1 = 0, plotY0 = 0, plotY1 = 0;
    static constexpr float kLeftSlotW = 32.0f;   // 每个左轴槽的 gutter 宽度（左右等宽）
    static constexpr float kRightSlotW = 32.0f;  // 每个右轴槽的 gutter 宽度
    static constexpr float kPlotMinW = 20.0f;    // gutter 计算的最小保护宽度
    int64_t visFirst = 0; int visCount = 0;
    std::vector<TSSeries> curVis;        // 本次可见序列（浮窗/求 max 用）
    int    curBw = 1;
    int    fixedBw = 0;                  // >0 时强制桶宽（分钟），覆盖 bucketForSpan（总览固定窗口用）

    static constexpr double kMinSpan = 10;              // 最小跨度（分钟）
    static constexpr double kMaxSpan = 365.0 * 1440 * 100;

    // ---- 精度阶梯：视口跨度(分钟) → 聚合桶宽(分钟) ----
    // 目标可见桶数约 60（落在 50~70 之间），桶宽取"nice"值，自动优化精度
    static int bucketForSpan(double spanMin) {
        double target = spanMin / 60.0;
        static const int nice[] = {1, 2, 3, 5, 6, 10, 12, 15, 20, 30, 40, 60, 90, 120, 180, 240, 360, 480, 720, 1440, 2880, 4320, 8640, 17280, 43200, 86400};
        for (int n : nice) {
            if ((double)n >= target) return n;
        }
        return 43200;   // 超长范围（≥1年）按月聚合
    }

    void setDataRange(double mn, double mx) {
        if (mx > mn) { dataMin = mn; dataMax = mx; }
    }
    // 清空全部精度缓存（数据源变更/日期切换后调用）
    void clearCaches() { caches.clear(); cache.clear(); maxVisInit = false; }
    // 仅失效数据桶缓存（保留纵轴 maxVis 动画状态），供实时增长数据周期性刷新
    void invalidateData() { caches.clear(); cache.clear(); }
    // 仅更新系列透明度目标（alpha 经 tick 淡入淡出），勾选切换后立即调用，
    // 否则首次 tick 时 alphaTarget 还是旧值、animating 被误清成 false，淡入淡出不启动。
    void setAlphaTargetToEnabled() {
        if (alpha.size() == 0) return;
        for (size_t i = 0; i < alpha.size(); ++i) {
            bool on = !enabled.empty() && std::find(enabled.begin(), enabled.end(), (int)i) != enabled.end();
            alphaTarget[i] = on ? 1.0f : 0.0f;
        }
    }

    // 设置目标窗口（滚轮/拖拽/切档统一入口），并平滑过渡
    void setTarget(double t, double s) {
        flingVel = 0.0;   // 缩放/切档时停止惯性滑动
        if (s < kMinSpan) s = kMinSpan;
        if (s > kMaxSpan) s = kMaxSpan;
        double lo = dataMin, hi = dataMax;
        if (hi <= lo) { hi = t + s + kMinSpan; lo = t - kMinSpan; }
        int bw = (fixedBw > 0) ? fixedBw : bucketForSpan(s);
        hi += bw;   // 右侧留一个桶余量，使"现在"（最右侧数据）完整可见
        if (s > hi - lo) s = hi - lo;   // 跨度不超过数据域：任意缩放档位都无两端空白
        if (t < lo) t = lo;
        if (t + s > hi) t = hi - s;
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

    void onDown(float x) { dragging = true; dragX = x; dragStartT = tStart; flingVel = 0.0; }
    void onUp() { dragging = false; }
    void onDrag(float x, bool inside) {
        if (!dragging) return;
        double dw = plotX1 - plotX0;
        if (dw <= 0) return;
        double nt = dragStartT - (double)(x - dragX) / dw * span;
        // 直连映射：拖拽期视口 1:1 跟手，不经动画插值
        double lo = dataMin, hi = dataMax;
        int bw = (fixedBw > 0) ? fixedBw : bucketForSpan(span);
        hi += bw;   // 右侧留一个桶余量，最右侧数据完整可见
        if (hi > lo) {
            double sp = span; if (sp > hi - lo) sp = hi - lo;
            if (nt < lo) nt = lo;
            if (nt + sp > hi) nt = hi - sp;
            if (nt < lo) nt = lo;
        }
        // 记录拖拽速度（分钟/帧），轻量 EMA 平滑，作为松手后的惯性动量
        flingVel = (nt - tStart) * 0.7 + flingVel * 0.3;
        // 限速：避免极快甩动产生过长的惯性滑行
        double cap = span * 0.5;
        if (flingVel >  cap) flingVel =  cap;
        if (flingVel < -cap) flingVel = -cap;
        tStart = nt; targetT = nt; animating = false;
        if (!inside) dragging = false;
    }
    // 悬停更新：返回 true 表示悬停状态发生变化（调用方应触发重绘）
    bool onMoveInside(float x, float y) {
        hoverX = x; hoverY = y;
        int b = -1;
        if (visCount > 0 && plotX1 > plotX0 &&
            x >= plotX0 && x <= plotX1 && y >= plotY0 && y <= plotY1) {
        float px = (x - plotX0) / (plotX1 - plotX0);
        int idx = (int)(px * bucketsPerPlot + panFrac);   // 加 panFrac 抵消拖拽分数偏移，命中正确桶
        if (idx < 0) idx = 0; if (idx >= visCount) idx = visCount - 1;
            b = idx;
        }
        if (b != hoverBucket) { hoverBucket = b; hoverActive = (b >= 0); return true; }
        return false;
    }
    void onLeave() { hoverBucket = -1; hoverActive = false; }

    // 每帧动画推进；返回 true 表示仍在动画（调用方应安排重绘）
    bool tick(double dtMs = 16.0) {
        // 时间缩放：动画速率与真实时间对齐，而非依赖固定帧率。
        // WM_TIMER 会因消息循环繁忙抖动到 30ms+，若用固定步长会导致动画时快时慢（卡顿感）。
        this->dtMs = (dtMs > 100.0 ? 100.0 : (dtMs < 1.0 ? 1.0 : dtMs));
        double k = 1.0 - std::pow(0.70, this->dtMs / 16.0);   // 16ms → 0.3；dt 越大单步越大，总速率不变
        if (k > 0.98) k = 0.98;
        double flingDecay = std::pow(0.50, this->dtMs / 16.0);

        // 惯性平移：松手后按剩余动量继续滑动，逐帧摩擦衰减（0.92 提供约 300ms 的滑行）
        if (!dragging && std::fabs(flingVel) > 0.05) {
            double lo = dataMin, hi = dataMax;
            int bw = (fixedBw > 0) ? fixedBw : bucketForSpan(span);
            hi += bw;
            double sp = span; if (sp > hi - lo) sp = hi - lo;
            double nt = tStart + flingVel;
            if (nt < lo) nt = lo;
            if (nt + sp > hi) nt = hi - sp;
            if (nt < lo) nt = lo;
            tStart = nt; targetT = nt;
            flingVel *= flingDecay;   // 快速衰减，滑行时长约 ≤100ms
            if (std::fabs(flingVel) < 0.05) flingVel = 0.0;
            return true;   // 惯性滑动中，继续重绘
        }
        bool viewDone = !animating;
        if (animating) {
            double nt = tStart + (targetT - tStart) * k;
            double ns = span + (targetSpan - span) * k;
            tStart = nt; span = ns;
            if (std::fabs(targetT - tStart) < 0.5 && std::fabs(targetSpan - span) < 0.5) {
                tStart = targetT; span = targetSpan;
                viewDone = true;
            }
        }
        // 系列透明度追赶（勾选渐入/渐出 ~200ms ease-out）
        bool alphaDone = true;
        for (size_t i = 0; i < alpha.size(); ++i) {
            float d = alphaTarget[i] - alpha[i];
            if (std::fabs(d) <= 0.01f) continue;
            alpha[i] += (float)(d * k);
            if (std::fabs(alphaTarget[i] - alpha[i]) < 0.02f) alpha[i] = alphaTarget[i];
            alphaDone = false;
        }
        animating = !(viewDone && alphaDone);
        return animating;
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

    // 按可见范围取数（多精度独立缓存）。画面外预加载：两侧各多缓存 margin 桶，
    // 平移/缩放时新桶已在缓存内、只做切片，避免每帧全量重算导致滑动锯齿。
    void ensureData(int bw, int64_t first, int cnt, bool dark) {
        if (cnt <= 0) cnt = 1;
        int margin = std::max(1, cnt / 4) + 2;
        int64_t fFirst = first - margin;
        int fCnt = cnt + 2 * margin;
        ++cacheTick;
        // 查同精度缓存（以带 margin 的填充窗口为准）
        auto it = caches.find(bw);
        if (it == caches.end() || it->second.data.empty()) {
            std::vector<TSSeries> full;
            if (fill) fill(bw, fFirst, fCnt, full, ud);
            CacheEntry e; e.data = std::move(full); e.first = fFirst; e.updatedTick = cacheTick;
            it = caches.emplace(bw, std::move(e)).first;
        } else {
            CacheEntry& e = it->second;
            int64_t cLast = e.first + (int64_t)e.data[0].v.size();
            if (fFirst >= e.first && (int64_t)(fFirst + fCnt) <= cLast) {
                // 完全命中，直接切片
            } else {
                std::vector<TSSeries> full;
                if (fill) fill(bw, fFirst, fCnt, full, ud);
                e.data = std::move(full); e.first = fFirst; e.updatedTick = cacheTick;
            }
        }
        // 别名指向命中精度的数据（避免拷贝）
        cache = it->second.data;
        int64_t cacheFirst = it->second.first;   // 该精度缓存起点（含 margin）
        // 简单淘汰：超过 16 个精度时清理最旧的
        if (caches.size() > 16) {
            int64_t oldest = cacheTick;
            int oldKey = -1;
            for (auto& kv : caches) if (kv.second.updatedTick < oldest) { oldest = kv.second.updatedTick; oldKey = kv.first; }
            if (oldKey >= 0 && oldKey != bw) caches.erase(oldKey);
        }
        // 维护系列透明度（D1 勾选渐入/渐出）：长度对齐 fill 输出系列数
        if (alpha.size() != cache.size()) {
            alpha.assign(cache.size(), 0.0f);
            alphaTarget.assign(cache.size(), 0.0f);
            // 首次直接对齐目标（无渐入动画），之后 enabled 变化经 tick 追赶
            for (size_t i = 0; i < cache.size(); ++i) {
                bool on = !enabled.empty() && std::find(enabled.begin(), enabled.end(), (int)i) != enabled.end();
                alpha[i] = alphaTarget[i] = on ? 1.0f : 0.0f;
            }
        } else {
            for (size_t i = 0; i < cache.size(); ++i) {
                bool on = !enabled.empty() && std::find(enabled.begin(), enabled.end(), (int)i) != enabled.end();
                alphaTarget[i] = on ? 1.0f : 0.0f;
            }
        }
        curVis.clear();
        int off = (int)(first - cacheFirst);   // = margin（可见窗口在缓存内的偏移）
        for (size_t i = 0; i < cache.size(); ++i) {
            // 渐入/渐出过渡中也绘制（alpha>0）；完全隐藏才跳过
            if (alpha[i] < 0.01f && alphaTarget[i] <= 0.0f) continue;
            TSSeries s;
            s.color = cache[i].color;
            // 暗色模式压暗系列色（×0.72，与热力图 heatColor 一致，避免深色背景上刺眼）
            if (dark) { s.color.r *= 0.72f; s.color.g *= 0.72f; s.color.b *= 0.72f; }
            s.name = cache[i].name; s.axis = cache[i].axis;
            s.alpha = alpha[i];
            // 多取 1 个桶：拖拽分数偏移时右边缘的半个桶有数据可画
            s.v.assign(cache[i].v.begin() + off, cache[i].v.begin() + off + cnt + 1);
            curVis.push_back(s);
        }
        visFirst = first; visCount = cnt; curBw = bw;
    }

    void draw(UiDrawCtx ctx, UiRect rect, bool dark);

    static UiColor tsColor(int r, int g, int b, int a = 255) {
        return UiColor{ r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    }
};

// 悬浮框绘制（首两行时间范围 + 后续系列行）
inline void drawTooltipImpl(TimeSeriesChart& c, UiDrawCtx ctx, UiRect rect,
                            std::vector<TSSeries>& curVis,
                            const std::wstring& tStart, const std::wstring& tEnd,
                            const std::vector<std::wstring>& lines);

// 绘制实现：背景 + 网格 + 纵轴 + 柱/折线 + 十字光标 + 悬浮浮窗
inline void TimeSeriesChart::draw(UiDrawCtx ctx, UiRect rect, bool dark) {
    UiColor axisCol = dark ? tsColor(140, 140, 140) : tsColor(138, 138, 140);
    UiColor gridCol = dark ? tsColor(58, 58, 58) : tsColor(235, 235, 235);

    int bw = (fixedBw > 0) ? fixedBw : bucketForSpan(span);
    int64_t g0 = (int64_t)std::floor(tStart / (double)bw) * bw;
    int64_t first = g0 / bw;
    int cnt = (int)std::ceil(span / (double)bw) + 1;   // 固定桶数，拖拽/缩放期间数量稳定
    if (cnt < 2) cnt = 2;
    if (cnt > 400) cnt = 400;
    bucketsPerPlot = span / (double)bw;               // 精确的"每绘图宽桶数"，横坐标与命中都用它
    // 窗口起点在桶内的分数偏移（拖拽时子桶平滑滑入，而非整桶跳变）
    panFrac = (tStart - (double)g0) / (double)bw;
    if (panFrac < 0.0 || panFrac >= 1.0) panFrac = 0.0;

    ensureData(bw, first, cnt, dark);

    // ---- D3 4 轴槽（L1=0, L2=1, R1=2, R2=3）：每轴独立 maxVis 动画 ----
    // axis=0..1 → 左；axis=2..3 → 右。未启用槽位不占宽。
    float maxV[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    UiColor axCol[4] = {
        tsColor(138, 138, 140), tsColor(138, 138, 140),
        tsColor(138, 138, 140), tsColor(138, 138, 140)
    };
    bool   axOn[4] = { false, false, false, false };
    float  axAlpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (auto& s : curVis) {
        int ax = s.axis; if (ax < 0) ax = 0; if (ax > 3) ax = 3;
        axOn[ax] = true;
        axCol[ax] = s.color;   // 该轴刻度文字着色为所属系列色（首个系列定色）
        if (s.alpha > axAlpha[ax]) axAlpha[ax] = s.alpha;   // 轴透明度随系列淡入淡出同步
        for (size_t i = 0; i < (size_t)(cnt + 1) && i < s.v.size(); ++i) {
            float v = s.v[i];
            if (v > maxV[ax]) maxV[ax] = v;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!axOn[i]) { maxVis[i] = 1.0f; continue; }
        maxV[i] *= 1.05f;
        if (maxV[i] < 1.0f) maxV[i] = 1.0f;
    }
    float axMax[4];
    bool drawAnim = false;   // 绘制期内仍在收敛的动效（纵轴），完成后需继续驱动重绘
    if (!maxVisInit) {
        for (int i = 0; i < 4; ++i) axMax[i] = maxVis[i] = maxV[i];
        maxVisInit = true;
    } else {
        // 纵轴始终平滑追赶（移动/峰值变化时纵坐标平滑出入，消除突变）
        // 同样按真实时间缩放，避免纵轴收敛比 alpha 慢、造成"末尾还在压/缩"的尾巴卡顿
        float kVis = (float)(1.0 - std::pow(0.70, dtMs / 16.0));
        if (kVis > 0.98f) kVis = 0.98f;
        for (int i = 0; i < 4; ++i) {
            if (!axOn[i]) { axMax[i] = maxVis[i] = 1.0f; continue; }
            axMax[i] = maxVis[i] += (maxV[i] - maxVis[i]) * kVis;
            if (std::fabs(maxV[i] - maxVis[i]) > maxV[i] * 0.02f) drawAnim = true;
        }
    }
    // 纵轴仍在收敛时并入 animating，避免动画停半途（需输入才续动）
    animating = animating || drawAnim;

    // ---- 动态 gutter：槽宽随系列透明度同步伸缩，淡出/淡入时轴槽与图象、轴标签严格同步收起/展开 ----
    int leftAxes[2] = { -1, -1 }, rightAxes[2] = { -1, -1 };
    int leftSlots = 0, rightSlots = 0;
    if (axOn[0]) leftAxes[leftSlots++] = 0;
    if (axOn[1]) leftAxes[leftSlots++] = 1;
    if (axOn[2]) rightAxes[rightSlots++] = 2;
    if (axOn[3]) rightAxes[rightSlots++] = 3;
    float sumL = 0.0f, sumR = 0.0f;
    for (int j = 0; j < leftSlots;  ++j) sumL += kLeftSlotW  * axAlpha[leftAxes[j]];
    for (int j = 0; j < rightSlots; ++j) sumR += kRightSlotW * axAlpha[rightAxes[j]];
    // 直接取 alpha 加权槽宽（axAlpha 已由 tick 逐帧平滑），使槽收回与淡出严格同帧，无 gk 滞后
    plotL = leftSlots  > 0 ? sumL : 6.0f;
    plotR = rightSlots > 0 ? sumR : 6.0f;

    float plotW = (rect.right - rect.left) - plotL - plotR;
    float plotH = (rect.bottom - rect.top) - plotT - plotB;
    if (plotW <= kPlotMinW || plotH <= 10) { hoverActive = false; return; }
    float px0 = rect.left + plotL, px1 = rect.right - plotR;
    float py0 = rect.top + plotT,  py1 = rect.bottom - plotB;
    plotX0 = px0; plotX1 = px1; plotY0 = py0; plotY1 = py1;

    // 网格 + 轴标签（图表背景透明，由外层卡片提供底色）
    for (int g = 0; g <= 3; ++g) {
        float gy = py1 - plotH * g / 3.0f;
        ui_draw_line(ctx, px0, gy, px1, gy, gridCol, 1.0f);
        wchar_t lbs[24];
        // 左轴：标签槽宽按轴透明度加权，从绘图区边缘向外排布（淡出时剩余轴随槽同步内移）
        float loff = 0.0f;
        for (int j = 0; j < leftSlots; ++j) {
            int ax = leftAxes[j];
            _snwprintf_s(lbs, 24, L"%s", tsFmtNum(axMax[ax] * g / 3.0f).c_str());
            float sw = kLeftSlotW * axAlpha[ax];
            float lx1 = px0 - loff, lx0 = lx1 - sw;
            UiColor ac = axCol[ax]; ac.a *= axAlpha[ax];
            ui_draw_text_ex(ctx, lbs, UiRect{ lx0, gy - 5, lx1, gy + 7 }, ac, 9, 2, 1);
            loff += sw;
        }
        // 右轴：标签槽宽按轴透明度加权，从绘图区边缘向外排布
        float roff = 0.0f;
        for (int j = 0; j < rightSlots; ++j) {
            int ax = rightAxes[j];
            _snwprintf_s(lbs, 24, L"%s", tsFmtNum(axMax[ax] * g / 3.0f).c_str());
            float sw = kRightSlotW * axAlpha[ax];
            float rx0 = px1 + roff, rx1 = rx0 + sw;
            UiColor ac = axCol[ax]; ac.a *= axAlpha[ax];
            ui_draw_text_ex(ctx, lbs, UiRect{ rx0, gy - 5, rx1, gy + 7 }, ac, 9, 2, 1);
            roff += sw;
        }
    }

    bool anyData = false;
    for (auto& s : curVis) for (float v : s.v) if (v > 0) { anyData = true; break; }
    if (!anyData) {
        // 无数据段落也延续绘制横坐标与柱状/折线（取消"暂无数据"占位），仅清空悬停
        hoverBucket = -1; hoverActive = false;
    }

    // 每桶按桶宽均分槽位，多系列并排（slot = 桶的像素宽 = plotW / 每图桶数）
    float slot = plotW / (float)bucketsPerPlot;
    size_t nSeries = curVis.size();
    if (nSeries == 0) return;
    // 柱状列宽直接由各系列透明度加权：勾选/取消时列宽随淡入淡出同步伸缩，
    // 与 alpha 同源、无独立 barCountVis 过渡动画，彻底消除"先淡出再挤压"的错位感。
    float sumAlpha = 0.0f;
    for (auto& s : curVis) sumAlpha += (s.alpha > 0.02f ? s.alpha : 0.0f);
    if (sumAlpha < 0.01f) sumAlpha = 0.01f;
    // 桶 b 的横向中心（减去 panFrac 实现拖拽时的子桶平滑滑入）
    auto bucketX = [&](int b) { return px0 + slot * ((float)b - (float)panFrac) + slot * 0.5f; };

    // 剪切到绘图区：拖拽分数偏移时边缘的半桶不越过纵轴 gutter / 右侧边界
    ui_draw_push_clip(ctx, UiRect{ px0, py0, px1, py1 });
    if (line) {
        // 曲线：单调三次 Hermite 样条（过点且不超调），分段近似
        auto drawCurve = [&](const std::vector<std::pair<float,float>>& pts, UiColor col, float width) {
            int n = (int)pts.size();
            if (n < 2) return;
            std::vector<float> dx(n - 1), d(n - 1);
            for (int i = 0; i < n - 1; ++i) {
                dx[i] = pts[i + 1].first - pts[i].first;
                d[i] = (dx[i] > 1e-6f) ? (pts[i + 1].second - pts[i].second) / dx[i] : 0.0f;
            }
            std::vector<float> tg(n, 0.0f);
            tg[0] = d[0];
            tg[n - 1] = n >= 2 ? d[n - 2] : 0.0f;
            for (int i = 1; i < n - 1; ++i) {
                if (d[i - 1] * d[i] <= 0.0f) { tg[i] = 0.0f; continue; }
                float w1 = 2.0f * dx[i] + dx[i - 1];
                float w2 = dx[i] + 2.0f * dx[i - 1];
                float den = w1 / d[i - 1] + w2 / d[i];
                tg[i] = (den > 1e-9f) ? (w1 + w2) / den : 0.0f;
            }
            const int STEPS = 8;
            for (int i = 0; i < n - 1; ++i) {
                float h = dx[i];
                if (h <= 1e-6f) continue;
                float px = pts[i].first, py = pts[i].second;
                for (int t = 1; t <= STEPS; ++t) {
                    float tt = (float)t / STEPS, tt2 = tt * tt, tt3 = tt2 * tt;
                    float h00 = 2.0f * tt3 - 3.0f * tt2 + 1.0f;
                    float h10 = tt3 - 2.0f * tt2 + tt;
                    float h01 = -2.0f * tt3 + 3.0f * tt2;
                    float h11 = tt3 - tt2;
                    float cx = pts[i].first + h * tt;
                    float cy = h00 * pts[i].second + h10 * h * tg[i] + h01 * pts[i + 1].second + h11 * h * tg[i + 1];
                    ui_draw_line(ctx, px, py, cx, cy, col, width);
                    px = cx; py = cy;
                }
            }
        };
        for (size_t si = 0; si < nSeries; ++si) {
            TSSeries& s = curVis[si];
            int ax = s.axis; if (ax < 0) ax = 0; if (ax > 3) ax = 3;
            float m = axMax[ax];
            UiColor sc = s.color; sc.a *= s.alpha;
            std::vector<std::pair<float,float>> pts;
            pts.reserve(cnt + 1);
            for (int b = 0; b <= cnt; ++b) {
                float v = (b < (int)s.v.size()) ? s.v[b] : 0.0f;
                pts.push_back({ bucketX(b), py1 - plotH * (v / m) });
            }
            drawCurve(pts, sc, 2.0f);
        }
    } else {
        // 柱状：组内列宽按各系列透明度加权分配，柱高乘 alpha（淡入淡出同步伸缩）
        for (int b = 0; b <= cnt; ++b) {
            float cx = bucketX(b);
            float gx = cx - slot * 0.4f;   // 组左边界
            for (size_t si = 0; si < nSeries; ++si) {
                TSSeries& s = curVis[si];
                float a = s.alpha;
                if (a < 0.02f) continue;   // 近乎透明：不绘制，也不占列宽
                int ax = s.axis; if (ax < 0) ax = 0; if (ax > 3) ax = 3;
                float m = axMax[ax];
                float v = (b < (int)s.v.size()) ? s.v[b] : 0.0f;
                float w = slot * 0.8f * (a / sumAlpha);
                if (w < 0.5f) w = 0.5f;
                if (v > 0) {
                    float hbar = plotH * (v / m) * a;
                    ui_draw_fill_rect(ctx, UiRect{ gx, py1 - hbar, gx + w, py1 }, s.color);
                }
                gx += w;
            }
        }
    }
    ui_draw_pop_clip(ctx);

    // 底部时间轴刻度：目标 ~6 个标签，且步长取"nice 因数"，
    // 保证每天从 0 点开始、每小时从 0 分开始（如 bw=60 时 step=4→0/4/8/…；
    // bw=1440 时 step=1→每天；bw=5 时 step=12→小时边界）。
    {
        int wantStep = cnt / 6;
        if (wantStep < 1) wantStep = 1;
        // nice 步长列表（都是 24 或 60 的因数或因子链）
        static const int niceSteps[] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 24, 48, 60, 72, 120, 144 };
        int step = niceSteps[sizeof(niceSteps)/sizeof(niceSteps[0]) - 1];
        for (int n : niceSteps) {
            if (n >= wantStep) { step = n; break; }
        }
        for (int b = 0; b < cnt; b += step) {
            int64_t gm = (visFirst + b) * (int64_t)bw;
            std::wstring l = labelFor(bw, gm);
            float cx = bucketX(b);
            UiRect lr = { cx - slot * step * 0.5f, py1 + 3, cx + slot * step * 0.5f, py1 + 18 };
            ui_draw_text_ex(ctx, l.c_str(), lr, axisCol, 9, 2, 0);
        }
    }

    // 十字光标：吸附最近桶中心 + 高亮 + 浮窗
    if (hoverActive && hoverBucket >= 0 && hoverBucket < cnt) {
        float ccx = bucketX(hoverBucket);
        ui_draw_line(ctx, ccx, py0, ccx, py1, tsColor(176, 186, 198, 200), 1.0f);
        if (line) {
            // 折线：各系列在命中桶处画圆点
            for (size_t si = 0; si < nSeries; ++si) {
                TSSeries& s = curVis[si];
                int ax = s.axis; if (ax < 0) ax = 0; if (ax > 3) ax = 3;
                float v = (hoverBucket < (int)s.v.size()) ? s.v[hoverBucket] : 0.0f;
                float m = axMax[ax];
                float cy = py1 - plotH * (v / m);
                ui_draw_fill_rounded_rect(ctx, UiRect{ ccx - 2.5f, cy - 2.5f, ccx + 2.5f, cy + 2.5f },
                                          2.5f, 2.5f, s.color);
            }
        } else {
            // 柱状：高亮该桶各系列柱（覆盖半透明描边，列宽与绘制一致按 alpha 加权）
            float gx = ccx - slot * 0.4f;
            for (size_t si = 0; si < nSeries; ++si) {
                TSSeries& s = curVis[si];
                float a = s.alpha;
                if (a < 0.02f) continue;
                int ax = s.axis; if (ax < 0) ax = 0; if (ax > 3) ax = 3;
                float v = (hoverBucket < (int)s.v.size()) ? s.v[hoverBucket] : 0.0f;
                float m = axMax[ax];
                float w = slot * 0.8f * (a / sumAlpha);
                if (w < 0.5f) w = 0.5f;
                if (v > 0) {
                    float hbar = plotH * (v / m);
                    ui_draw_rounded_rect(ctx, UiRect{ gx, py1 - hbar, gx + w, py1 },
                                         0.0f, 0.0f, tsColor(255, 255, 255, 130), 1.0f);
                }
                gx += w;
            }
        }
        // 浮窗：首两行为时间范围（第一行"起始-"，第二行"结束"），其后各系列精确数值
        int64_t gm0 = (visFirst + hoverBucket) * (int64_t)bw;
        std::wstring tStart = labelFor(bw, gm0) + L"-";
        std::wstring tEnd = labelFor(bw, gm0 + bw);
        std::vector<std::wstring> lines;
        for (size_t si = 0; si < nSeries; ++si) {
            TSSeries& s = curVis[si];
            if (s.alpha < 0.5f) continue;   // 浮窗只列 alpha>0.5 的系列（渐出中不列）
            float v = (hoverBucket < (int)s.v.size()) ? s.v[hoverBucket] : 0.0f;
            wchar_t vb[40];
            if (v >= 1000000000.0f)   _snwprintf_s(vb, 40, L"%.2fB", v / 1e9f);
            else if (v >= 1000000.0f) _snwprintf_s(vb, 40, L"%.1fM", v / 1e6f);
            else                      _snwprintf_s(vb, 40, L"%.0f", v);
            lines.push_back(s.name + L" " + vb);
        }
        drawTooltipImpl(*this, ctx, rect, curVis, tStart, tEnd, lines);
    }
}

// 悬浮框绘制（首两行时间范围 + 后续系列行）
inline void drawTooltipImpl(TimeSeriesChart& c, UiDrawCtx ctx, UiRect rect,
                            std::vector<TSSeries>& curVis,
                            const std::wstring& tStart, const std::wstring& tEnd,
                            const std::vector<std::wstring>& lines) {
    float maxW = ui_draw_measure_text(ctx, tStart.c_str(), 12);
    float w2 = ui_draw_measure_text(ctx, tEnd.c_str(), 12); if (w2 > maxW) maxW = w2;
    for (auto& ln : lines) { float w = ui_draw_measure_text(ctx, ln.c_str(), 12); if (w > maxW) maxW = w; }
    float bw2 = maxW + 20;
    int rows = 2 + (int)lines.size();
    float bh = 12.0f + rows * 16.0f;
    float by = c.hoverY - bh - 8; if (by < rect.top) by = c.hoverY + 12;
    float bx = c.hoverX - bw2 - 10; if (bx < rect.left) bx = c.hoverX + 10;
    UiRect br = { bx, by, bx + bw2, by + bh };
    ui_draw_fill_rounded_rect(ctx, br, 5.0f, 5.0f, TimeSeriesChart::tsColor(24, 26, 31, 235));
    ui_draw_rounded_rect(ctx, br, 5.0f, 5.0f, TimeSeriesChart::tsColor(120, 126, 136, 120), 1.0f);
    float yy = br.top + 6;
    ui_draw_text_ex(ctx, tStart.c_str(), UiRect{ br.left + 10, yy, br.right - 4, yy + 16 },
                    TimeSeriesChart::tsColor(255, 255, 255), 12, 2, 0);
    yy += 16;
    ui_draw_text_ex(ctx, tEnd.c_str(), UiRect{ br.left + 10, yy, br.right - 4, yy + 16 },
                    TimeSeriesChart::tsColor(160, 166, 176), 12, 2, 0);
    yy += 16;
    for (size_t i = 0; i < lines.size() && i < curVis.size(); ++i) {
        ui_draw_text_ex(ctx, lines[i].c_str(),
                        UiRect{ br.left + 10, yy, br.right - 4, yy + 16 },
                        curVis[i].color, 12, 2, 0);
        yy += 16;
    }
}
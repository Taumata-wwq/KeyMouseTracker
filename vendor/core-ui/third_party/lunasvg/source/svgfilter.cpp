#include "svgfilter.h"

#include <plutovg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lunasvg {

namespace {

// 零填充 1D box blur, 滑动窗口 O(n)。out 须已 == in.size() (调用方预分配);
// 每个输出元素都被写, 故不预清零 (省去每趟 memset — 旧版每趟 assign 清零
// 是 4096² 阴影 4 秒的主因之一)。
void boxBlur1D(const std::vector<float>& in, std::vector<float>& out,
               int w, int h, int r, bool horizontal)
{
    const float norm = 1.0f / (2 * r + 1);
    if(horizontal) {
        for(int y = 0; y < h; ++y) {
            const float* row = &in[static_cast<size_t>(y) * w];
            float* orow = &out[static_cast<size_t>(y) * w];
            float acc = 0.f;
            for(int x = 0; x <= r && x < w; ++x)
                acc += row[x];
            for(int x = 0; x < w; ++x) {
                orow[x] = acc * norm;
                int add = x + r + 1, rem = x - r;
                if(add < w) acc += row[add];
                if(rem >= 0) acc -= row[rem];
            }
        }
    } else {
        for(int x = 0; x < w; ++x) {
            float acc = 0.f;
            for(int y = 0; y <= r && y < h; ++y)
                acc += in[static_cast<size_t>(y) * w + x];
            for(int y = 0; y < h; ++y) {
                out[static_cast<size_t>(y) * w + x] = acc * norm;
                int add = y + r + 1, rem = y - r;
                if(add < h) acc += in[static_cast<size_t>(add) * w + x];
                if(rem >= 0) acc -= in[static_cast<size_t>(rem) * w + x];
            }
        }
    }
}

// 三次 box-blur 逼近高斯, 原地做; scratch 由调用方预分配 (== a 尺寸), 跨趟复用。
void blur3(std::vector<float>& a, std::vector<float>& scratch,
           int w, int h, float sigma)
{
    if(sigma <= 0.f || w <= 0 || h <= 0)
        return;
    int d = static_cast<int>(std::floor(sigma * 3.0 * std::sqrt(2.0 * 3.14159265358979) / 4.0 + 0.5));
    int r = std::max(1, d / 2);
    for(int pass = 0; pass < 3; ++pass) {
        boxBlur1D(a, scratch, w, h, r, true);    // a -> scratch (H)
        boxBlur1D(scratch, a, w, h, r, false);   // scratch -> a (V)
    }
}

inline uint8_t clamp8(float v)
{
    return static_cast<uint8_t>(std::clamp(v, 0.f, 255.f));
}

// 双线性采样 small[sw×sh] 在 (gx, gy)(small 坐标系), 边缘 clamp。
inline float sampleBilinear(const std::vector<float>& s, int sw, int sh,
                            float gx, float gy)
{
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const float wx = gx - x0, wy = gy - y0;
    const int x0c = std::clamp(x0,     0, sw - 1);
    const int x1c = std::clamp(x0 + 1, 0, sw - 1);
    const int y0c = std::clamp(y0,     0, sh - 1);
    const int y1c = std::clamp(y0 + 1, 0, sh - 1);
    const float* r0 = &s[static_cast<size_t>(y0c) * sw];
    const float* r1 = &s[static_cast<size_t>(y1c) * sw];
    const float top = r0[x0c] * (1.f - wx) + r0[x1c] * wx;
    const float bot = r1[x0c] * (1.f - wx) + r1[x1c] * wx;
    return top * (1.f - wy) + bot * wy;
}

} // namespace

void applyDropShadow(Canvas& canvas, float dx, float dy, float sigma,
                     const Color& floodColor, float floodOpacity)
{
    auto* surface = canvas.surface();
    if(surface == nullptr)
        return;
    const int w = plutovg_surface_get_width(surface);
    const int h = plutovg_surface_get_height(surface);
    const int stride = plutovg_surface_get_stride(surface);
    uint8_t* data = plutovg_surface_get_data(surface);
    if(data == nullptr || w <= 0 || h <= 0)
        return;

    /* 降采样因子: 大 sigma 在 1/f 分辨率做模糊 (阴影低频, 视觉无损), 封顶 8。 */
    const int f  = std::clamp(static_cast<int>(sigma / 2.0f), 1, 8);
    const int sw = (w + f - 1) / f;
    const int sh = (h + f - 1) / f;

    /* 1) 直接从 surface 的 alpha 通道 box-average 降采样到 small —— 省掉旧版的
     *    全分辨率 alpha 平面提取 (一趟全图读写)。 */
    std::vector<float> small(static_cast<size_t>(sw) * sh, 0.f);
    std::vector<float> cnt(static_cast<size_t>(sw) * sh, 0.f);
    for(int y = 0; y < h; ++y) {
        const uint8_t* row = data + static_cast<size_t>(y) * stride;
        float* srow = &small[static_cast<size_t>(y / f) * sw];
        float* crow = &cnt[static_cast<size_t>(y / f) * sw];
        for(int x = 0; x < w; ++x) { srow[x / f] += row[x * 4 + 3]; crow[x / f] += 1.f; }
    }
    for(size_t i = 0; i < small.size(); ++i)
        small[i] = cnt[i] > 0.f ? small[i] / (cnt[i] * 255.f) : 0.f;

    /* 2) 小图三次 box-blur (sigma/f)。 */
    std::vector<float> scratch(small.size());
    blur3(small, scratch, sw, sh, sigma / f);

    /* 3) 合成: out = content over shadow。shadow 覆盖直接从 small 双线性采样 ——
     *    省掉旧版的全分辨率上采 (又一趟全图写)。shadow 在 (x,y) 取 content
     *    (x-dx, y-dy) 的模糊覆盖 → small 坐标 ((x-dx)+0.5)/f-0.5。 */
    const float fr = floodColor.redF(), fg = floodColor.greenF(), fb = floodColor.blueF();
    const float inv_f = 1.0f / static_cast<float>(f);
    for(int y = 0; y < h; ++y) {
        uint8_t* row = data + static_cast<size_t>(y) * stride;
        const float gy = (static_cast<float>(y) - dy + 0.5f) * inv_f - 0.5f;
        for(int x = 0; x < w; ++x) {
            const float gx = (static_cast<float>(x) - dx + 0.5f) * inv_f - 0.5f;
            const float sa = sampleBilinear(small, sw, sh, gx, gy) * floodOpacity;

            uint8_t* p = &row[x * 4];   // 内容预乘 [B,G,R,A]
            const float cA = p[3];
            const float ica = 1.f - cA / 255.f;   // content over: 阴影只透出未被内容覆盖处
            p[0] = clamp8(p[0] + fb * sa * 255.f * ica);
            p[1] = clamp8(p[1] + fg * sa * 255.f * ica);
            p[2] = clamp8(p[2] + fr * sa * 255.f * ica);
            p[3] = clamp8(cA + sa * 255.f * ica);
        }
    }
}

void applyGaussianBlur(Canvas& canvas, float sigma)
{
    if(sigma <= 0.f)
        return;
    auto* surface = canvas.surface();
    if(surface == nullptr)
        return;
    const int w = plutovg_surface_get_width(surface);
    const int h = plutovg_surface_get_height(surface);
    const int stride = plutovg_surface_get_stride(surface);
    uint8_t* data = plutovg_surface_get_data(surface);
    if(data == nullptr || w <= 0 || h <= 0)
        return;

    const size_t count = static_cast<size_t>(w) * h;
    std::vector<float> channel(count);
    std::vector<float> scratch(count);

    for(int c = 0; c < 4; ++c) {
        for(int y = 0; y < h; ++y) {
            const uint8_t* row = data + static_cast<size_t>(y) * stride;
            float* out = &channel[static_cast<size_t>(y) * w];
            for(int x = 0; x < w; ++x)
                out[x] = row[x * 4 + c];
        }

        blur3(channel, scratch, w, h, sigma);

        for(int y = 0; y < h; ++y) {
            uint8_t* row = data + static_cast<size_t>(y) * stride;
            const float* in = &channel[static_cast<size_t>(y) * w];
            for(int x = 0; x < w; ++x)
                row[x * 4 + c] = clamp8(in[x]);
        }
    }
}

} // namespace lunasvg

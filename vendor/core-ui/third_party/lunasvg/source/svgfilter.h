#ifndef LUNASVG_SVGFILTER_H
#define LUNASVG_SVGFILTER_H

// gh-svg (L173 Phase 3): SVG 滤镜实现 — LunaSVG 上游不支持 <filter>, 这是 gh-svg
// 在其渲染管线 (beginGroup/endGroup 离屏合成) 之上叠加的自研滤镜层。
//
// 在 endGroup 里, 元素已渲染进一个离屏 Canvas (按膨胀后的 filter region 尺寸),
// 这里对该 surface 原地应用滤镜, 再由调用方 blend 回父画布。
//
// 当前覆盖 feDropShadow、blur-only feGaussianBlur、以及常见
// SourceAlpha -> feOffset -> feGaussianBlur -> feColorMatrix 阴影链。

#include "graphics.h"

namespace lunasvg {

// 对 canvas 的离屏 surface 原地应用 feDropShadow:
//   取内容覆盖 (alpha) → 反向偏移采样 (dx,dy 设备像素) + 高斯模糊 (sigma 设备
//   像素, 三次 box-blur 近似) → flood-color×flood-opacity 上色 → 内容 over 阴影。
// 预乘 ARGB32 空间运算 (plutovg native)。
//
// 注: v1 在 sRGB 预乘空间做模糊/合成; SVG 规范滤镜默认 linearRGB。对深色阴影
// 视觉差异很小, linearRGB 精确化留作后续 (color-interpolation-filters 支持时)。
void applyDropShadow(Canvas& canvas, float dx, float dy, float sigma,
                     const Color& floodColor, float floodOpacity);
void applyGaussianBlur(Canvas& canvas, float sigma);

} // namespace lunasvg

#endif // LUNASVG_SVGFILTER_H

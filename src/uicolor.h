#pragma once
#include <ui_core.h>

// 0-255 整型 → UiColor（内部 0..1 浮点）。色阶工具的共用底座，
// 供热力图（heatColor）与时间序列图表（此前 tsColor 为其重复实现）共用。
inline UiColor rgb255(int r, int g, int b, int a = 255) {
    return UiColor{ r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
}

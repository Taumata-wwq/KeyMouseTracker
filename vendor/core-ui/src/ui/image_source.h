#pragma once
#include "renderer.h"
#include <d2d1.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <functional>
#include <cstdint>

/*
 * IImageSource — ImageViewPlus 后端抽象
 *
 * 每种图像类型（静态位图 / SVG 矢量 / GIF 动图 / 分块大图 / 后续扩展）实现
 * 一个派生类。ImageViewPlus 只管视口（zoom/pan/rotation）、交互（mouse/key）、
 * crop overlay，真正的内容绘制委托给 source->Draw(ctx)。
 *
 * 扩展新后端：
 *   1. 写一个 派生类 override Width/Height/Draw
 *   2. 在 image_source.cpp 的 CreateFromFile 里按扩展名/magic 分派
 *
 * 约束：Draw 收到的 dest 是 widget 坐标系内、已经算好 zoom/rotate 中心的最终矩形。
 *       source 不关心 widget 的 rect/pan；它只需要"把自己画到这个 dest 里"。
 */

namespace ui {

using Microsoft::WRL::ComPtr;

// 绘制上下文 —— 打包传给 source，避免 Draw 签名炸开
struct ImageDrawContext {
    D2D1_RECT_F   dest;           // 目标矩形（已含 zoom 和居中）
    float         zoom = 1.0f;    // 当前缩放（矢量/动态插值选择需要）
    int           rotation = 0;   // 0/90/180/270
    bool          antialias = false;
    bool          checkerboard = true;  // 透明背景是否画棋盘
};

// 源能力标志 —— widget 用来决定是否画棋盘/能否 tile 等
struct ImageCaps {
    bool vector   = false;   // 矢量后端，缩放零糊
    bool animated = false;   // 需要 Tick 驱动
    bool tiled    = false;   // 自己管分块，widget 不额外处理大图
    bool alpha    = false;   // 内容含透明通道（决定是否需要棋盘）
};

class IImageSource {
public:
    virtual ~IImageSource() = default;

    // 原始内容像素尺寸（rotate 前）
    virtual int Width()  const = 0;
    virtual int Height() const = 0;

    virtual ImageCaps Caps() const = 0;

    // 绘制到 ctx.dest 内。widget 已 PushClip，source 不用自己裁剪。
    virtual void Draw(Renderer& r, const ImageDrawContext& ctx) = 0;

    // 动画驱动（非动图默认 no-op）。返回 true 表示有新帧需要重绘。
    // dt 单位毫秒。widget 的 timer 统一驱动。
    virtual bool Tick(double /*dtMs*/) { return false; }

    // 动图元信息
    virtual int  FrameCount()   const { return 1; }
    virtual int  CurrentFrame() const { return 0; }
    virtual void SeekFrame(int /*i*/) {}

    // 调试名，用于日志
    virtual const char* TypeName() const = 0;

    // ---- 工厂 ----

    // 按扩展名 + magic 字节探测，返回对应 source。
    // 失败返回 nullptr（调用方决定显示占位/错误）。
    static std::unique_ptr<IImageSource>
        CreateFromFile(const std::wstring& path, Renderer& r);

    // 从内存像素（BGRA）创建静态位图源
    static std::unique_ptr<IImageSource>
        CreateFromPixels(const void* pixels, int w, int h, int stride, Renderer& r);

    // 空的分块源。使用者后续 SetTile 填充。
    static std::unique_ptr<IImageSource>
        CreateTiled(int fullW, int fullH, int tileSize, Renderer& r);

    // 分块源的扩展接口（由 TiledSource 派生后 dynamic_cast 访问）
    class ITiledSource {
    public:
        virtual ~ITiledSource() = default;
        virtual void SetTile(int tx, int ty, const void* pixels,
                             int w, int h, int stride) = 0;
        virtual void ClearTiles() = 0;
        virtual int  TileSize() const = 0;
    };
};

// SVG 的 fallback 接口 —— 当 D2D < 1607 没 ID2D1DeviceContext5 时，
// 退化到 Renderer::ParseSvgIcon 路径（功能子集：只支持 path + basic opacity）
class ISvgFallbackSource : public IImageSource {
public:
    // 可以暴露 SvgIcon 便于外部测试/复用
    virtual const SvgIcon& Icon() const = 0;
};

} // namespace ui

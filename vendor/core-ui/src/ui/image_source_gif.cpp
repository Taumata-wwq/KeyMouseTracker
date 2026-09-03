#include "image_source.h"
#include "renderer.h"
#include "resource_store.h"
#include <atomic>
#include <memory>
#include <utility>

namespace ui {

namespace {

uint64_t NextGifSourceGeneration() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

ImageSampling SamplingForGif(D2D1_BITMAP_INTERPOLATION_MODE interp) {
    return interp == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
        ? ImageSampling::Nearest
        : ImageSampling::Linear;
}

} // namespace

/*
 * GifSource —— 用 Renderer::AnimatedPlayer 按需解码。
 * 相比 ImageViewWidget 的做法把 timer 移到 widget，source 只提供 Tick。
 * 当前帧写入 ResourceStore，DisplayList 只记录 ResourceKey；D2D bitmap
 * 只是有即时 RT 时的可选缓存。
 */
class GifSource : public IImageSource {
public:
    GifSource(std::unique_ptr<Renderer::AnimatedPlayer> player,
              ComPtr<ID2D1Bitmap> bmp,
              ResourceKey resourceKey,
              uint64_t generation)
        : player_(std::move(player)),
          bmp_(std::move(bmp)),
          resourceKey_(resourceKey),
          generation_(generation) {
        if (player_) {
            w_ = player_->CanvasWidth();
            h_ = player_->CanvasHeight();
        }
    }

    ~GifSource() override {
        if (resourceKey_.IsValid()) GlobalResourceStore().Remove(resourceKey_);
    }

    int Width()  const override { return w_; }
    int Height() const override { return h_; }
    ImageCaps Caps() const override {
        ImageCaps c; c.animated = (player_ && player_->FrameCount() > 1);
        c.alpha = true;  // GIF 几乎都有透明色
        return c;
    }
    const char* TypeName() const override { return "GifSource"; }

    int  FrameCount()   const override { return player_ ? player_->FrameCount() : 1; }
    int  CurrentFrame() const override { return currentFrame_; }
    void SeekFrame(int i) override {
        if (!player_) return;
        int fc = player_->FrameCount();
        if (fc <= 0) return;
        currentFrame_ = ((i % fc) + fc) % fc;
        Upload();
        accumMs_ = 0;
    }

    bool Tick(double dtMs) override {
        if (!player_ || player_->FrameCount() <= 1) return false;
        accumMs_ += dtMs;
        int delay = player_->DelayMs(currentFrame_);
        if (delay <= 0) delay = 100;
        if (accumMs_ < delay) return false;
        accumMs_ -= delay;
        currentFrame_ = (currentFrame_ + 1) % player_->FrameCount();
        Upload();
        return true;
    }

    void Draw(Renderer& r, const ImageDrawContext& ctx) override {
        if (!bmp_ && resourceKey_.IsValid()) {
            auto res = GlobalResourceStore().Acquire(resourceKey_);
            if (res && res->bytes) {
                bmp_ = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
            }
        }
        if (!bmp_ && !resourceKey_.IsValid()) return;
        auto interp = ctx.antialias
            ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
            : (ctx.zoom >= 1.0f
                ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        const ImageSampling sampling = SamplingForGif(interp);
        if (ctx.rotation == 0) {
            r.RecordImage(resourceKey_, ctx.dest, sampling);
            if (bmp_) r.DrawBitmap(bmp_.Get(), ctx.dest, 1.0f, interp);
            return;
        }

        float dcx = (ctx.dest.left + ctx.dest.right ) / 2.0f;
        float dcy = (ctx.dest.top  + ctx.dest.bottom) / 2.0f;
        float drawW = ctx.dest.right  - ctx.dest.left;
        float drawH = ctx.dest.bottom - ctx.dest.top;
        float effW = (ctx.rotation == 90 || ctx.rotation == 270) ? (float)h_ : (float)w_;
        float effH = (ctx.rotation == 90 || ctx.rotation == 270) ? (float)w_ : (float)h_;
        auto xf =
            D2D1::Matrix3x2F::Translation(-(float)w_/2, -(float)h_/2) *
            D2D1::Matrix3x2F::Rotation((float)ctx.rotation) *
            D2D1::Matrix3x2F::Scale(drawW / effW, drawH / effH) *
            D2D1::Matrix3x2F::Translation(dcx, dcy);
        r.PushTransform(xf);
        D2D1_RECT_F src = {0, 0, (float)w_, (float)h_};
        r.RecordImage(resourceKey_, src, sampling);
        if (bmp_) r.DrawBitmap(bmp_.Get(), src, 1.0f, interp);
        r.PopTransform();
    }

private:
    void Upload() {
        const uint8_t* px = player_->ComposeTo(currentFrame_);
        if (px) {
            const int stride = w_ * 4;
            if (resourceKey_.IsValid()) {
                if (!GlobalResourceStore().UpdateImage(resourceKey_, w_, h_, stride,
                                                       PixelFormat::BgraPremul, px, true)) {
                    resourceKey_ = {};
                }
            }
            if (!resourceKey_.IsValid()) {
                resourceKey_ = GlobalResourceStore().AddImage(
                    ResourceKind::Bitmap, generation_, w_, h_, stride,
                    PixelFormat::BgraPremul, px, true);
            }
            if (bmp_) bmp_->CopyFromMemory(nullptr, px, (UINT)stride);
        }
    }

    std::unique_ptr<Renderer::AnimatedPlayer> player_;
    ComPtr<ID2D1Bitmap> bmp_;
    ResourceKey resourceKey_;
    uint64_t generation_ = 0;
    int w_ = 0, h_ = 0;
    int currentFrame_ = 0;
    double accumMs_ = 0;
};

// ========= 工厂 =========

std::unique_ptr<IImageSource>
CreateGifSourceFromFile(const std::wstring& path, Renderer& r) {
    auto player = r.OpenAnimatedImage(path);
    if (!player) return nullptr;
    // 创建初始纹理
    const uint8_t* px = player->ComposeTo(0);
    if (!px) return nullptr;
    int w = player->CanvasWidth();
    int h = player->CanvasHeight();
    int stride = w * 4;
    uint64_t generation = NextGifSourceGeneration();
    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap, generation, w, h, stride,
        PixelFormat::BgraPremul, px, true);
    if (!resourceKey.IsValid()) return nullptr;
    auto res = GlobalResourceStore().Acquire(resourceKey);
    if (!res || !res->bytes) {
        GlobalResourceStore().Remove(resourceKey);
        return nullptr;
    }
    auto bmp = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
    return std::make_unique<GifSource>(std::move(player), bmp, resourceKey, generation);
}

} // namespace ui

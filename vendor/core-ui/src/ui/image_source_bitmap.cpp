#include "image_source.h"
#include "display_list.h"
#include "renderer.h"
#include "resource_store.h"
#include <atomic>
#include <utility>
#include <vector>

namespace ui {

namespace {

uint64_t NextBitmapSourceGeneration() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

ImageSampling SamplingForBitmap(bool useHQ, D2D1_BITMAP_INTERPOLATION_MODE interp) {
    if (useHQ) return ImageSampling::HighQualityCubic;
    return interp == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
        ? ImageSampling::Nearest
        : ImageSampling::Linear;
}

} // namespace

class BitmapSource : public IImageSource {
public:
    explicit BitmapSource(ComPtr<ID2D1Bitmap> bmp, ResourceKey resourceKey = {},
                          int resourceW = 0, int resourceH = 0, bool resourceHasAlpha = true)
        : bmp_(std::move(bmp)), resourceKey_(resourceKey) {
        if (bmp_) InitFromBitmap();
        else {
            w_ = resourceW;
            h_ = resourceH;
            hasAlpha_ = resourceHasAlpha;
        }
    }

    ~BitmapSource() override {
        if (resourceKey_.IsValid()) GlobalResourceStore().Remove(resourceKey_);
    }

    int Width()  const override { return w_; }
    int Height() const override { return h_; }
    ImageCaps Caps() const override {
        ImageCaps c; c.alpha = hasAlpha_; return c;
    }
    const char* TypeName() const override { return "BitmapSource"; }

    void Draw(Renderer& r, const ImageDrawContext& ctx) override {
        if (!bmp_ && resourceKey_.IsValid()) {
            auto res = GlobalResourceStore().Acquire(resourceKey_);
            if (res && res->bytes) {
                bmp_ = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
            }
        }
        if (!bmp_ && !resourceKey_.IsValid()) return;

        // 与 ImageViewWidget 相同插值策略：放大 NEAREST（锐利）/ 缩小 HIGH_QUALITY_CUBIC
        bool useHQ = (ctx.zoom < 1.0f) || ctx.antialias;
        auto interp = (!ctx.antialias && ctx.zoom >= 1.0f)
            ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
        const ImageSampling sampling = SamplingForBitmap(useHQ, interp);

        if (ctx.rotation == 0) {
            r.RecordImage(resourceKey_, ctx.dest, sampling);
            if (!bmp_) return;
            if (useHQ) r.DrawBitmapHQ(bmp_.Get(), ctx.dest, 1.0f,
                                       D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            else       r.DrawBitmap  (bmp_.Get(), ctx.dest, 1.0f, interp);
            return;
        }

        float drawW = ctx.dest.right - ctx.dest.left;
        float drawH = ctx.dest.bottom - ctx.dest.top;
        float effW = (ctx.rotation == 90 || ctx.rotation == 270) ? (float)h_ : (float)w_;
        float effH = (ctx.rotation == 90 || ctx.rotation == 270) ? (float)w_ : (float)h_;
        float sx = drawW / effW;
        float sy = drawH / effH;
        float dcx = (ctx.dest.left + ctx.dest.right ) / 2.0f;
        float dcy = (ctx.dest.top  + ctx.dest.bottom) / 2.0f;

        auto xf =
            D2D1::Matrix3x2F::Translation(-(float)w_/2, -(float)h_/2) *
            D2D1::Matrix3x2F::Rotation((float)ctx.rotation) *
            D2D1::Matrix3x2F::Scale(sx, sy) *
            D2D1::Matrix3x2F::Translation(dcx, dcy);
        r.PushTransform(xf);

        D2D1_RECT_F src = {0, 0, (float)w_, (float)h_};
        r.RecordImage(resourceKey_, src, sampling);
        if (!bmp_) {
            r.PopTransform();
            return;
        }
        if (useHQ)
            r.DrawBitmapHQ(bmp_.Get(), src, 1.0f,
                            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        else
            r.DrawBitmap(bmp_.Get(), src, 1.0f, interp);

        r.PopTransform();
    }

private:
    void InitFromBitmap() {
        auto sz = bmp_->GetPixelSize();
        w_ = static_cast<int>(sz.width);
        h_ = static_cast<int>(sz.height);
        auto fmt = bmp_->GetPixelFormat();
        hasAlpha_ = (fmt.alphaMode == D2D1_ALPHA_MODE_PREMULTIPLIED ||
                     fmt.alphaMode == D2D1_ALPHA_MODE_STRAIGHT);
    }

    ComPtr<ID2D1Bitmap> bmp_;
    ResourceKey resourceKey_;
    int w_ = 0, h_ = 0;
    bool hasAlpha_ = false;
};

// ========= 工厂 =========

std::unique_ptr<IImageSource>
CreateBitmapSourceFromFile(const std::wstring& path, Renderer& r) {
    std::vector<uint8_t> pixels;
    int w = 0, h = 0, stride = 0;
    if (r.DecodeImageFileToBgraPremul(path, pixels, w, h, stride)) {
        ResourceKey resourceKey = GlobalResourceStore().AddImage(
            ResourceKind::Bitmap, NextBitmapSourceGeneration(), w, h, stride,
            PixelFormat::BgraPremul, pixels.data(), true);
        auto res = GlobalResourceStore().Acquire(resourceKey);
        if (!res || !res->bytes) {
            if (resourceKey.IsValid()) GlobalResourceStore().Remove(resourceKey);
            return nullptr;
        }
        auto bmp = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
        return std::make_unique<BitmapSource>(bmp, resourceKey, res->width, res->height, true);
    }

    return nullptr;
}

std::unique_ptr<IImageSource>
CreateBitmapSourceFromPixels(const void* px, int w, int h, int stride, Renderer& r) {
    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap, NextBitmapSourceGeneration(), w, h, stride,
        PixelFormat::BgraPremul, px, true);
    auto res = GlobalResourceStore().Acquire(resourceKey);
    if (!res || !res->bytes) return nullptr;

    auto bmp = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
    return std::make_unique<BitmapSource>(bmp, resourceKey, res->width, res->height, true);
}

} // namespace ui

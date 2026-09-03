#include "image_source.h"
#include "display_list.h"
#include "renderer.h"
#include "resource_store.h"
#include <atomic>
#include <map>
#include <cmath>
#include <algorithm>

namespace ui {

namespace {

uint64_t NextTiledSourceGeneration() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

class TiledSource : public IImageSource, public IImageSource::ITiledSource {
public:
    TiledSource(int fullW, int fullH, int tileSize, Renderer& r)
        : fullW_(fullW), fullH_(fullH),
          tileSize_(tileSize > 0 ? tileSize : 512),
          renderer_(&r),
          generation_(NextTiledSourceGeneration()) {}

    ~TiledSource() override {
        ClearTiles();
    }

    int Width()  const override { return fullW_; }
    int Height() const override { return fullH_; }
    ImageCaps Caps() const override {
        ImageCaps c; c.tiled = true; return c;
    }
    const char* TypeName() const override { return "TiledSource"; }

    void Draw(Renderer& r, const ImageDrawContext& ctx) override {
        // 计算可见瓦片范围
        float drawW = ctx.dest.right  - ctx.dest.left;
        float drawH = ctx.dest.bottom - ctx.dest.top;
        if (drawW <= 0 || drawH <= 0) return;

        int ts = tileSize_;
        int txMax = (fullW_ + ts - 1) / ts;
        int tyMax = (fullH_ + ts - 1) / ts;

        // 外扩 1 像素 + NEAREST 防缝
        auto interp = D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;

        for (int ty = 0; ty < tyMax; ++ty) {
            for (int tx = 0; tx < txMax; ++tx) {
                auto it = tiles_.find({tx, ty});
                if (it == tiles_.end()) continue;
                int tw = it->second.w;
                int th = it->second.h;
                float x0 = std::floor(ctx.dest.left + (tx * ts)      * ctx.zoom) - 1.0f;
                float y0 = std::floor(ctx.dest.top  + (ty * ts)      * ctx.zoom) - 1.0f;
                float x1 = std::ceil (ctx.dest.left + (tx * ts + tw) * ctx.zoom) + 1.0f;
                float y1 = std::ceil (ctx.dest.top  + (ty * ts + th) * ctx.zoom) + 1.0f;
                D2D1_RECT_F dest = {x0, y0, x1, y1};
                r.RecordImage(it->second.resourceKey, dest, ImageSampling::Nearest);
                r.DrawBitmap(it->second.bmp.Get(), dest, 1.0f, interp);
            }
        }
    }

    // ---- ITiledSource ----
    void SetTile(int tx, int ty, const void* pixels,
                 int w, int h, int stride) override {
        if (!renderer_ || !pixels || w <= 0 || h <= 0) return;
        ResourceKey resourceKey = GlobalResourceStore().AddImage(
            ResourceKind::Tile, generation_, w, h, stride,
            PixelFormat::BgraPremul, pixels, true);
        auto res = GlobalResourceStore().Acquire(resourceKey);
        if (!res || !res->bytes) return;

        auto bmp = renderer_->CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
        if (!bmp) {
            GlobalResourceStore().Remove(resourceKey);
            return;
        }

        auto key = std::make_pair(tx, ty);
        auto old = tiles_.find(key);
        if (old != tiles_.end()) GlobalResourceStore().Remove(old->second.resourceKey);
        tiles_[key] = {bmp, resourceKey, w, h};
    }
    void ClearTiles() override {
        for (const auto& kv : tiles_) {
            GlobalResourceStore().Remove(kv.second.resourceKey);
        }
        tiles_.clear();
    }
    int  TileSize() const override { return tileSize_; }

private:
    struct Tile {
        ComPtr<ID2D1Bitmap> bmp;
        ResourceKey resourceKey;
        int w = 0, h = 0;
    };
    int fullW_, fullH_, tileSize_;
    Renderer* renderer_;
    uint64_t generation_;
    std::map<std::pair<int,int>, Tile> tiles_;
};

std::unique_ptr<IImageSource>
CreateTiledSource(int fullW, int fullH, int tileSize, Renderer& r) {
    return std::make_unique<TiledSource>(fullW, fullH, tileSize, r);
}

} // namespace ui

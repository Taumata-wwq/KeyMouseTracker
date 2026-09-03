#pragma once

#include "render_handles.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ui {

struct CpuImageResource {
    ResourceKey key;
    int width = 0;
    int height = 0;
    int stride = 0;
    PixelFormat format = PixelFormat::BgraPremul;
    std::shared_ptr<const std::vector<uint8_t>> bytes;
    size_t byte_size = 0;
    bool pinned_visible = false;
    bool evictable = false;
};

class ResourceStore {
public:
    ResourceKey AddImage(ResourceKind kind,
                         uint64_t imageGeneration,
                         int width,
                         int height,
                         int stride,
                         PixelFormat format,
                         const void* pixels,
                         bool pinnedVisible = false);

    bool UpdateImage(const ResourceKey& key,
                     int width,
                     int height,
                     int stride,
                     PixelFormat format,
                     const void* pixels,
                     bool pinnedVisible = false);

    std::shared_ptr<const CpuImageResource> Acquire(const ResourceKey& key) const;
    void SetPinnedVisible(const ResourceKey& key, bool pinned);
    void Remove(const ResourceKey& key);
    void MarkGenerationEvictable(uint64_t imageGeneration);
    void PurgeGeneration(uint64_t imageGeneration);
    void Clear();

    void SetByteBudget(size_t bytes);
    size_t ByteBudget() const;
    size_t TotalBytes() const;

private:
    static int BytesPerPixel(PixelFormat format);
    void EvictOverBudgetLocked();

    mutable std::mutex mutex_;
    std::unordered_map<ResourceKey, std::shared_ptr<CpuImageResource>, ResourceKeyHash> resources_;
    uint64_t nextResourceId_ = 1;
    size_t totalBytes_ = 0;
    size_t byteBudget_ = 512ull * 1024ull * 1024ull;
};

ResourceStore& GlobalResourceStore();

} // namespace ui

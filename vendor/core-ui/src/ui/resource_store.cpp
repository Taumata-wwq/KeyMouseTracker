#include "resource_store.h"
#include "debug_trace.h"

#include <algorithm>
#include <cstring>

namespace ui {

namespace {

bool CheckedImageByteSize(int width, int height, int stride, int bpp, size_t& out) {
    if (width <= 0 || height <= 0 || bpp <= 0) return false;
    const size_t rowBytes = static_cast<size_t>(width) * static_cast<size_t>(bpp);
    if (stride <= 0) stride = static_cast<int>(rowBytes);
    if (static_cast<size_t>(stride) < rowBytes) return false;
    const size_t h = static_cast<size_t>(height);
    if (h > (static_cast<size_t>(-1) / static_cast<size_t>(stride))) return false;
    out = static_cast<size_t>(stride) * h;
    return true;
}

} // namespace

int ResourceStore::BytesPerPixel(PixelFormat format) {
    switch (format) {
    case PixelFormat::BgraPremul:
    case PixelFormat::BgraStraight:
    case PixelFormat::Rgba:
        return 4;
    }
    return 0;
}

ResourceKey ResourceStore::AddImage(ResourceKind kind,
                                    uint64_t imageGeneration,
                                    int width,
                                    int height,
                                    int stride,
                                    PixelFormat format,
                                    const void* pixels,
                                    bool pinnedVisible) {
    const int bpp = BytesPerPixel(format);
    size_t byteSize = 0;
    if (!pixels || !CheckedImageByteSize(width, height, stride, bpp, byteSize)) {
        return {};
    }
    if (stride <= 0) stride = width * bpp;

    auto bytes = std::make_shared<std::vector<uint8_t>>(byteSize);
    std::memcpy(bytes->data(), pixels, byteSize);

    auto resource = std::make_shared<CpuImageResource>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resource->key = ResourceKey{imageGeneration, nextResourceId_++, kind};
        resource->width = width;
        resource->height = height;
        resource->stride = stride;
        resource->format = format;
        resource->bytes = std::move(bytes);
        resource->byte_size = byteSize;
        resource->pinned_visible = pinnedVisible;

        totalBytes_ += resource->byte_size;
        resources_[resource->key] = resource;

        TraceEvent("resource_store", "resource_store_add",
                   {TraceU64("generation", resource->key.image_generation),
                    TraceU64("resource_id", resource->key.resource_id),
                    TraceI64("kind", static_cast<int64_t>(resource->key.kind)),
                    TraceI64("bytes", static_cast<int64_t>(resource->byte_size)),
                    TraceI64("total_bytes", static_cast<int64_t>(totalBytes_)),
                    TraceBool("pinned_visible", resource->pinned_visible)});

        EvictOverBudgetLocked();
        return resource->key;
    }
}

bool ResourceStore::UpdateImage(const ResourceKey& key,
                                int width,
                                int height,
                                int stride,
                                PixelFormat format,
                                const void* pixels,
                                bool pinnedVisible) {
    if (!key.IsValid()) return false;
    const int bpp = BytesPerPixel(format);
    size_t byteSize = 0;
    if (!pixels || !CheckedImageByteSize(width, height, stride, bpp, byteSize)) {
        return false;
    }
    if (stride <= 0) stride = width * bpp;

    auto bytes = std::make_shared<std::vector<uint8_t>>(byteSize);
    std::memcpy(bytes->data(), pixels, byteSize);

    auto resource = std::make_shared<CpuImageResource>();
    resource->key = key;
    resource->width = width;
    resource->height = height;
    resource->stride = stride;
    resource->format = format;
    resource->bytes = std::move(bytes);
    resource->byte_size = byteSize;
    resource->pinned_visible = pinnedVisible;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(key);
    if (it == resources_.end()) return false;

    const size_t oldBytes = it->second->byte_size;
    resource->evictable = it->second->evictable;
    resources_[key] = resource;
    totalBytes_ -= std::min(totalBytes_, oldBytes);
    totalBytes_ += resource->byte_size;

    TraceEvent("resource_store", "resource_store_update",
               {TraceU64("generation", key.image_generation),
                TraceU64("resource_id", key.resource_id),
                TraceI64("kind", static_cast<int64_t>(key.kind)),
                TraceI64("bytes", static_cast<int64_t>(resource->byte_size)),
                TraceI64("total_bytes", static_cast<int64_t>(totalBytes_)),
                TraceBool("pinned_visible", resource->pinned_visible)});

    EvictOverBudgetLocked();
    return true;
}

std::shared_ptr<const CpuImageResource> ResourceStore::Acquire(const ResourceKey& key) const {
    if (!key.IsValid()) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(key);
    if (it == resources_.end()) {
        TraceEvent("resource_store", "resource_store_acquire_miss",
                   {TraceU64("generation", key.image_generation),
                    TraceU64("resource_id", key.resource_id),
                    TraceI64("kind", static_cast<int64_t>(key.kind))});
        return nullptr;
    }
    return it->second;
}

void ResourceStore::SetPinnedVisible(const ResourceKey& key, bool pinned) {
    if (!key.IsValid()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(key);
    if (it != resources_.end()) {
        it->second->pinned_visible = pinned;
        TraceEvent("resource_store", "resource_store_pin",
                   {TraceU64("generation", key.image_generation),
                    TraceU64("resource_id", key.resource_id),
                    TraceI64("kind", static_cast<int64_t>(key.kind)),
                    TraceBool("pinned_visible", pinned),
                    TraceI64("total_bytes", static_cast<int64_t>(totalBytes_))});
        EvictOverBudgetLocked();
    }
}

void ResourceStore::Remove(const ResourceKey& key) {
    if (!key.IsValid()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(key);
    if (it == resources_.end()) return;
    const size_t bytes = it->second->byte_size;
    totalBytes_ -= std::min(totalBytes_, bytes);
    resources_.erase(it);
    TraceEvent("resource_store", "resource_store_evict",
               {TraceU64("generation", key.image_generation),
                TraceU64("resource_id", key.resource_id),
                TraceI64("kind", static_cast<int64_t>(key.kind)),
                TraceI64("bytes", static_cast<int64_t>(bytes)),
                TraceI64("total_bytes", static_cast<int64_t>(totalBytes_))});
}

void ResourceStore::MarkGenerationEvictable(uint64_t imageGeneration) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : resources_) {
        if (kv.first.image_generation == imageGeneration) {
            kv.second->evictable = true;
        }
    }
    EvictOverBudgetLocked();
}

void ResourceStore::PurgeGeneration(uint64_t imageGeneration) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t removed = 0;
    for (auto it = resources_.begin(); it != resources_.end();) {
        if (it->first.image_generation == imageGeneration) {
            totalBytes_ -= std::min(totalBytes_, it->second->byte_size);
            removed += it->second->byte_size;
            it = resources_.erase(it);
        } else {
            ++it;
        }
    }
    TraceEvent("resource_store", "resource_store_generation_purged",
               {TraceU64("generation", imageGeneration),
                TraceI64("bytes", static_cast<int64_t>(removed)),
                TraceI64("total_bytes", static_cast<int64_t>(totalBytes_))});
}

void ResourceStore::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    resources_.clear();
    totalBytes_ = 0;
}

void ResourceStore::SetByteBudget(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    byteBudget_ = bytes;
    TraceEvent("resource_store", "resource_store_budget",
               {TraceI64("budget_bytes", static_cast<int64_t>(byteBudget_)),
                TraceI64("total_bytes", static_cast<int64_t>(totalBytes_))});
    EvictOverBudgetLocked();
}

size_t ResourceStore::ByteBudget() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return byteBudget_;
}

size_t ResourceStore::TotalBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalBytes_;
}

void ResourceStore::EvictOverBudgetLocked() {
    if (totalBytes_ <= byteBudget_) return;
    for (auto it = resources_.begin(); it != resources_.end() && totalBytes_ > byteBudget_;) {
        const auto& res = it->second;
        if (!res->pinned_visible && res->evictable) {
            const size_t bytes = res->byte_size;
            const ResourceKey key = it->first;
            totalBytes_ -= std::min(totalBytes_, bytes);
            it = resources_.erase(it);
            TraceEvent("resource_store", "resource_store_evict",
                       {TraceU64("generation", key.image_generation),
                        TraceU64("resource_id", key.resource_id),
                        TraceI64("kind", static_cast<int64_t>(key.kind)),
                        TraceI64("bytes", static_cast<int64_t>(bytes)),
                        TraceI64("total_bytes", static_cast<int64_t>(totalBytes_))});
        } else {
            ++it;
        }
    }
}

ResourceStore& GlobalResourceStore() {
    static ResourceStore store;
    return store;
}

} // namespace ui

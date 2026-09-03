#pragma once

#include <cstdint>
#include <functional>

namespace ui {

enum class ResourceKind : uint8_t {
    Preview,
    Tile,
    SvgRaster,
    Bitmap,
    Icon,
};

enum class PixelFormat : uint8_t {
    BgraPremul,
    BgraStraight,
    Rgba,
};

struct ResourceKey {
    uint64_t image_generation = 0;
    uint64_t resource_id = 0;
    ResourceKind kind = ResourceKind::Bitmap;

    bool IsValid() const { return resource_id != 0; }
    bool operator==(const ResourceKey& other) const noexcept {
        return image_generation == other.image_generation &&
               resource_id == other.resource_id &&
               kind == other.kind;
    }
};

struct ResourceKeyHash {
    size_t operator()(const ResourceKey& key) const noexcept {
        uint64_t h = key.image_generation;
        h ^= key.resource_id + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        h ^= static_cast<uint64_t>(key.kind) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        return static_cast<size_t>(h);
    }
};

} // namespace ui

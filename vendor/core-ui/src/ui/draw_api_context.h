#pragma once

#include "renderer.h"
#include "resource_store.h"

#include <cstdint>
#include <vector>

namespace ui {

class DisplayListRecorder;

struct DrawApiContext {
    static constexpr uint32_t kMagic = 0x44524157u; // "DRAW"

    explicit DrawApiContext(Renderer& r) : renderer(&r) {}
    DrawApiContext(Renderer* r, DisplayListRecorder* rec) : renderer(r), recorder(rec) {}
    ~DrawApiContext() {
        for (const auto& key : transientResources) {
            GlobalResourceStore().Remove(key);
        }
    }

    uint32_t magic = kMagic;
    Renderer* renderer = nullptr;
    DisplayListRecorder* recorder = nullptr;
    std::vector<ResourceKey> transientResources;
};

} // namespace ui

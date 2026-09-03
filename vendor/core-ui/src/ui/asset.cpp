#include "asset.h"

#include <ui_core.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace ui::asset {
namespace {

// ============================================================================
// 注册条目（支持三种来源）
// ============================================================================

enum class EntryKind { Dir, Blob, Resolver };

struct Entry {
    EntryKind kind;

    // Dir
    std::string dir;

    // Blob
    std::string blobName;
    const void* blobBytes = nullptr;
    size_t      blobSize  = 0;

    // Resolver
    UiAssetResolver fn = nullptr;
    void*           userdata = nullptr;
};

struct State {
    std::mutex mu;
    std::vector<Entry> entries;
};

State& G() {
    static State s;
    return s;
}

// 文件系统 owner：持有读出的字节直到析构
struct FileOwner : DataOwner {
    std::vector<unsigned char> data;
};

// Resolver 路径：让回调自己管生命周期，我们只持一个回调
struct ResolverOwner : DataOwner {
    // 回调注册了 free 钩子的话由它清理；目前回调返回的 bytes 必须在
    // 整个进程生命周期都有效（同 blob 语义）。这个 owner 就是空壳。
};

bool TryDir(const std::string& dir,
            const std::string& name,
            const void** out_bytes,
            size_t* out_size,
            DataOwnerPtr* out_owner) {
    namespace fs = std::filesystem;
    fs::path p = fs::path(dir) / name;
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return false;

    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return false;
    auto size = (std::streamsize)in.tellg();
    if (size < 0) return false;
    in.seekg(0, std::ios::beg);

    auto owner = std::make_unique<FileOwner>();
    owner->data.resize((size_t)size);
    if (size > 0 && !in.read(reinterpret_cast<char*>(owner->data.data()), size)) return false;

    *out_bytes = owner->data.data();
    *out_size  = owner->data.size();
    *out_owner = std::move(owner);
    return true;
}

}  // namespace

bool Resolve(const std::string& name,
             const void** out_bytes,
             size_t* out_size,
             DataOwnerPtr* out_owner) {
    if (!out_bytes || !out_size || !out_owner) return false;

    // 抓快照避免持锁期间做 IO
    std::vector<Entry> snapshot;
    {
        std::lock_guard<std::mutex> lk(G().mu);
        snapshot = G().entries;
    }

    for (const auto& e : snapshot) {
        switch (e.kind) {
        case EntryKind::Dir:
            if (TryDir(e.dir, name, out_bytes, out_size, out_owner)) return true;
            break;
        case EntryKind::Blob:
            if (e.blobName == name) {
                *out_bytes = e.blobBytes;
                *out_size  = e.blobSize;
                *out_owner = nullptr;     // blob bytes 由注册方保活
                return true;
            }
            break;
        case EntryKind::Resolver: {
            const void* bytes = nullptr;
            size_t size = 0;
            if (e.fn && e.fn(name.c_str(), &bytes, &size, e.userdata) && bytes) {
                *out_bytes = bytes;
                *out_size  = size;
                *out_owner = std::make_unique<ResolverOwner>();
                return true;
            }
            break;
        }
        }
    }
    return false;
}

void Reset() {
    std::lock_guard<std::mutex> lk(G().mu);
    G().entries.clear();
}

}  // namespace ui::asset

// ============================================================================
// C API
// ============================================================================

extern "C" {

UI_API void ui_asset_register_dir(const char* dir_utf8) {
    if (!dir_utf8 || !*dir_utf8) return;
    ui::asset::Entry e;
    e.kind = ui::asset::EntryKind::Dir;
    e.dir  = dir_utf8;
    std::lock_guard<std::mutex> lk(ui::asset::G().mu);
    ui::asset::G().entries.push_back(std::move(e));
}

UI_API void ui_asset_register_blob(const char* name,
                                   const void* bytes,
                                   size_t size) {
    if (!name || !*name || !bytes || size == 0) return;
    ui::asset::Entry e;
    e.kind      = ui::asset::EntryKind::Blob;
    e.blobName  = name;
    e.blobBytes = bytes;
    e.blobSize  = size;
    std::lock_guard<std::mutex> lk(ui::asset::G().mu);
    ui::asset::G().entries.push_back(std::move(e));
}

UI_API void ui_asset_register_resolver(UiAssetResolver fn, void* userdata) {
    if (!fn) return;
    ui::asset::Entry e;
    e.kind     = ui::asset::EntryKind::Resolver;
    e.fn       = fn;
    e.userdata = userdata;
    std::lock_guard<std::mutex> lk(ui::asset::G().mu);
    ui::asset::G().entries.push_back(std::move(e));
}

UI_API void ui_asset_reset(void) {
    ui::asset::Reset();
}

}  // extern "C"

#pragma once

// ui::asset — 全局资源解析器（内部 C++ API）
//
// 用途：HTML 里 <img src="logo.png"> / <link href="theme.css"> 这类按"名字"
// 引用资源时，统一通过这里查找。上层应用负责注册"名字 → 字节"的映射，库
// 不假设任何路径或文件系统结构。
//
// 注册渠道（按注册顺序串成 chain，先注册先匹配）：
//   1. 文件系统目录   ui_asset_register_dir("assets/")
//   2. 内存 blob     ui_asset_register_blob("logo.png", bytes, size)
//   3. 自定义回调    ui_asset_register_resolver(fn, userdata)
//
// dev 工作流通常注册 1（边改边看），ship 工作流注册 2（CMake embed_binary
// 烤进 exe，单文件分发）。两者**不需要改一行 HTML**。
//
// 内部 C++ 入口（widget_factory / image_source / page_compiler 用）：
//   bool ui::asset::Resolve(name, out_bytes, out_size, out_owner)
//   - out_owner 是必要的"生命周期 token"：blob 路径 owner 是空（指向静态字节），
//     dir 路径 owner 持有 mmap / heap buffer 防止解析期间被释放
//
// 所有 API 线程安全（内部 mutex 保护）；但解析返回的字节生命周期跟 owner
// 一致，调用方读完就要释放 owner。

#include <cstddef>
#include <memory>
#include <string>

namespace ui::asset {

// 资源数据生命周期持有者。Resolve 返回成功时，bytes 指针至少在 owner
// 析构之前有效。owner 可以为空（blob 路径，bytes 指向静态/外部 buffer）。
struct DataOwner {
    virtual ~DataOwner() = default;
};
using DataOwnerPtr = std::unique_ptr<DataOwner>;

// 解析 name 为字节流。成功返回 true 并填充 out_*。失败（未注册任何来源
// 或所有来源都说找不到）返回 false。
bool Resolve(const std::string& name,
             const void** out_bytes,
             size_t* out_size,
             DataOwnerPtr* out_owner);

// 重置全部注册（测试 / re-init 用）。
void Reset();

}  // namespace ui::asset

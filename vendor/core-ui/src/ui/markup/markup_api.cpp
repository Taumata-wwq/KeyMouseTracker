// markup_api.cpp — C API for the .ui markup loader (ui::UiMarkup).
//
// 解决"DLL 里有 UiMarkup 实现但 SDK 没发头"的问题：通过句柄 + C 包装把
// 整个 UiMarkup 公开成 C 可消费 API，下游不用 #include 任何内部 C++ 头。
//
// 句柄类型：UiLayout（uint64_t），生命周期 ui_layout_create / destroy。
// 与现有 UiPage、UiWidget 类似的注册表设计。

#include <ui_core.h>
#include "markup.h"
#include "../ui_context.h"
#include "../ui_window.h"
#include "../widget.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>

namespace ui::markup {

class LayoutRegistry {
public:
    uint64_t Insert(std::unique_ptr<UiMarkup> m) {
        std::lock_guard<std::mutex> lk(m_);
        uint64_t id = next_++;
        entries_[id] = std::move(m);
        return id;
    }
    UiMarkup* Get(uint64_t id) {
        std::lock_guard<std::mutex> lk(m_);
        auto it = entries_.find(id);
        return it == entries_.end() ? nullptr : it->second.get();
    }
    void Erase(uint64_t id) {
        std::lock_guard<std::mutex> lk(m_);
        entries_.erase(id);
    }
private:
    std::mutex m_;
    std::unordered_map<uint64_t, std::unique_ptr<UiMarkup>> entries_;
    uint64_t next_ = 1;
};

static LayoutRegistry& Registry() {
    static LayoutRegistry r;
    return r;
}

// 将 Widget* 转 UiWidget 句柄：先查复用，没找到就新插入。
// markup 构建的 widget 默认不在 handle table 里。
static UiWidget WidgetToHandle(Widget* w) {
    if (!w) return UI_INVALID;
    uint64_t h = ui::GetContext().handles.FindHandle(w);
    if (h) return h;
    return ui::GetContext().handles.Insert(w->shared_from_this());
}

} // namespace ui::markup

using ui::markup::Registry;

// ================================================================
// 生命周期 / 加载 / 错误
// ================================================================

UI_API UiLayout ui_layout_create(void) {
    return Registry().Insert(std::make_unique<ui::UiMarkup>());
}

UI_API void ui_layout_destroy(UiLayout l) {
    Registry().Erase(l);
}

UI_API int ui_layout_load_file(UiLayout l, const wchar_t* path) {
    auto* m = Registry().Get(l);
    if (!m || !path) return 0;
    return m->LoadFile(path) ? 1 : 0;
}

UI_API int ui_layout_load_string(UiLayout l, const char* utf8_source) {
    auto* m = Registry().Get(l);
    if (!m || !utf8_source) return 0;
    return m->LoadString(std::string(utf8_source)) ? 1 : 0;
}

UI_API const char* ui_layout_last_error(UiLayout l) {
    auto* m = Registry().Get(l);
    return m ? m->LastError().c_str() : "";
}

// ================================================================
// 树访问
// ================================================================

UI_API UiWidget ui_layout_root(UiLayout l) {
    auto* m = Registry().Get(l);
    if (!m) return UI_INVALID;
    auto root = m->Root();
    return root ? ui::markup::WidgetToHandle(root.get()) : UI_INVALID;
}

UI_API UiWidget ui_layout_find_by_id(UiLayout l, const char* id) {
    auto* m = Registry().Get(l);
    if (!m || !id) return UI_INVALID;
    return ui::markup::WidgetToHandle(m->FindById(id));
}

// ================================================================
// 一步式：layout + window 提示 + 创建窗口 + 挂根
//   等同于 demo/app.cpp 里那一坨手写胶水（读 hints → 拼 cfg → create →
//   set_root → show）。绝大多数下游"只想加载一个 .ui 显示出来"的场景这就够了。
// ================================================================

UI_API UiWindow ui_layout_open_window(UiLayout l, const UiWindowConfig* override_defaults) {
    auto* m = Registry().Get(l);
    if (!m) return UI_INVALID;
    const auto& wh = m->Window();

    UiWindowConfig cfg = {0};
    if (override_defaults) cfg = *override_defaults;
    if (cfg.width  == 0) cfg.width  = wh.width  > 0 ? wh.width  : 800;
    if (cfg.height == 0) cfg.height = wh.height > 0 ? wh.height : 600;
    if (!cfg.title) {
        // hints.title 是 std::string；C API 要 wchar_t*。先存到 thread-local
        // wstring 里保活到 ui_window_create 调用结束。
        thread_local std::wstring tlsTitle;
        if (!wh.title.empty()) {
            tlsTitle.assign(wh.title.begin(), wh.title.end());
            cfg.title = tlsTitle.c_str();
        } else {
            cfg.title = L"Core UI";
        }
    }
    if (cfg.resizable == 0 && wh.resizable >= 0) cfg.resizable = wh.resizable;

    UiWindow win = ui_window_create(&cfg);
    if (!win) return UI_INVALID;

    auto root = m->Root();
    if (root) {
        UiWidget h = ui::markup::WidgetToHandle(root.get());
        ui_window_set_root(win, h);
    }
    ui_window_show(win);
    return win;
}

// ================================================================
// 事件 handler — 按签名分 5 组（C 不能直接吃 std::function）
// 用 user_data 给 C callback 传上下文
// ================================================================

UI_API void ui_layout_set_handler_void(UiLayout l, const char* name,
                                        void (*fn)(void*), void* user) {
    auto* m = Registry().Get(l);
    if (!m || !name || !fn) return;
    // 显式声明 std::function 类型，否则 lambda 可能歧义匹配多条 SetHandler 重载
    std::function<void()> cb = [fn, user]() { fn(user); };
    m->SetHandler(name, cb);
}

UI_API void ui_layout_set_handler_bool(UiLayout l, const char* name,
                                        void (*fn)(int, void*), void* user) {
    auto* m = Registry().Get(l);
    if (!m || !name || !fn) return;
    std::function<void(bool)> cb = [fn, user](bool v) { fn(v ? 1 : 0, user); };
    m->SetHandler(name, cb);
}

UI_API void ui_layout_set_handler_int(UiLayout l, const char* name,
                                       void (*fn)(int, void*), void* user) {
    auto* m = Registry().Get(l);
    if (!m || !name || !fn) return;
    std::function<void(int)> cb = [fn, user](int v) { fn(v, user); };
    m->SetHandler(name, cb);
}

UI_API void ui_layout_set_handler_float(UiLayout l, const char* name,
                                         void (*fn)(double, void*), void* user) {
    auto* m = Registry().Get(l);
    if (!m || !name || !fn) return;
    std::function<void(float)> cb = [fn, user](float v) { fn(static_cast<double>(v), user); };
    m->SetHandler(name, cb);
}

UI_API void ui_layout_set_handler_text(UiLayout l, const char* name,
                                        void (*fn)(const wchar_t*, void*), void* user) {
    auto* m = Registry().Get(l);
    if (!m || !name || !fn) return;
    std::function<void(const std::wstring&)> cb =
        [fn, user](const std::wstring& v) { fn(v.c_str(), user); };
    m->SetHandler(name, cb);
}

// ================================================================
// 数据绑定（按 binding name 推值给 widget）
// ================================================================

UI_API void ui_layout_set_bool(UiLayout l, const char* name, int value) {
    auto* m = Registry().Get(l);
    if (m && name) m->SetBool(name, value != 0);
}
UI_API void ui_layout_set_int(UiLayout l, const char* name, int value) {
    auto* m = Registry().Get(l);
    if (m && name) m->SetInt(name, value);
}
UI_API void ui_layout_set_float(UiLayout l, const char* name, double value) {
    auto* m = Registry().Get(l);
    if (m && name) m->SetFloat(name, static_cast<float>(value));
}
UI_API void ui_layout_set_text(UiLayout l, const char* name, const wchar_t* value) {
    auto* m = Registry().Get(l);
    if (m && name) m->SetText(name, value ? value : L"");
}

// ================================================================
// i18n
// ================================================================

UI_API int ui_layout_load_language_string(UiLayout l, const char* utf8_content) {
    auto* m = Registry().Get(l);
    if (!m || !utf8_content) return 0;
    return m->LoadLanguageString(std::string(utf8_content)) ? 1 : 0;
}

UI_API int ui_layout_load_language_file(UiLayout l, const wchar_t* path) {
    auto* m = Registry().Get(l);
    if (!m || !path) return 0;
    return m->LoadLanguage(path) ? 1 : 0;
}

UI_API void ui_layout_apply_language(UiLayout l) {
    auto* m = Registry().Get(l);
    if (m) m->ApplyLanguage();
}

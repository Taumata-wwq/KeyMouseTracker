#pragma once
#include "compiled_page.h"
#include "../context_menu.h"
#include "../expression/value.h"     // expr::Value still used by ApplyBindingToWidget
#include "../uix/script_runtime.h"   // QuickJS ScriptRuntime + JSValue

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui::page {

// Runtime host for a compiled .uix page. Owns the QuickJS runtime, the
// reactive proxy on the data object, all binding/event/loop/conditional
// runtimes, plus the menu / window plumbing.
class PageState {
public:
    PageState();
    ~PageState();

    PageState(const PageState&) = delete;
    PageState& operator=(const PageState&) = delete;

    // Install a compiled page. Spins up a fresh ScriptRuntime + reactive
    // proxy + binding effects + v-if/v-for runtimes. Existing variable
    // values set via Set* are preserved across Attach (hot reload).
    void Attach(CompiledPage page);

    // Process-wide override for the QuickJS path. The flag is now mostly
    // historical (every page goes through QuickJS); kept for env-var
    // diff-bisecting and source compatibility.
    static void SetGlobalUseQuickJS(bool v);
    static bool GetGlobalUseQuickJS();

    // Called by ui_page_open_window after window creation; wires <menu>
    // triggers + <menuitem onclick> dispatchers that need the HWND.
    void AttachWindow(uint64_t winHandle);

    // v-if/v-for newly-mounted subtrees may carry <menu> children — give
    // them the same trigger/dispatch wiring as compile-time menus.
    void WireSubtreeMenus(const std::vector<CompiledMenu>& subMenus,
                          std::vector<std::shared_ptr<ContextMenu>>& outMenus);

    // Access root widget.
    WidgetPtr Root() const { return page_.root; }

    // Access compiled page data (window hints, etc.)
    const CompiledPage& PageData() const { return page_; }

    // Set / get reactive variables (by name). Writes go through the proxy
    // set-trap, triggering every effect that reads the same key.
    void SetBool(const std::string& name, bool v);
    void SetNumber(const std::string& name, double v);
    void SetString(const std::string& name, const std::string& v);
    void SetValue(const std::string& name, ui::expr::Value v);

    bool GetValue(const std::string& name, ui::expr::Value& out) const;

    // ---- Internationalization ----
    void LoadTranslations(const std::string& locale,
                          const std::unordered_map<std::string, std::string>& pairs);
    void SetLocale(const std::string& locale);
    const std::string& CurrentLocale() const { return currentLocale_; }
    std::string Translate(const std::string& key) const;

    // Errors (compile + runtime).
    const std::vector<std::string>& Errors() const { return errors_; }

    // ---- Lifecycle hooks (Vue parity for v-if / v-for remount) ----
    // Register a callback that fires every time a widget with the given
    // `id` is mounted (initial render, v-if truthy, v-for iteration build).
    // Multiple registrations on the same id replace the previous one.
    // Callback receives the freshly-mounted widget — DO NOT cache the
    // pointer past the matching unmount.
    void OnWidgetMount(const std::string& widgetId,
                       std::function<void(Widget*)> cb);
    // Symmetric: fires just before the widget is removed from its parent.
    void OnWidgetUnmount(const std::string& widgetId,
                          std::function<void(Widget*)> cb);

    // ---- Theme ----
    // Called after `ui_theme_set_mode` flips Light/Dark. Rebuilds the
    // library-provided CSS vars (--bg / --fg / --accent / ...) in the page's
    // shared cssVars table and re-runs the cascade on every widget so any
    // `var(--xxx)` reference resolves against the new theme.
    void RefreshThemeStyles();

    // ---- Menu lookup ----
    void WireMenus();
    ContextMenu* FindMenuById(const std::string& id) const;
    void InvalidateMenuHandles();
    uint64_t GetOrRegisterMenuHandle(const std::string& name);

private:
    // CondInIter (defined in page_state.cpp) holds nested v-for loop runtimes
    // for the v-for > v-if > v-for case. It needs to name the private nested
    // type JsLoopRuntime in its own field.
    friend struct CondInIter;

    CompiledPage page_;
    std::vector<std::string> errors_;
    std::string  currentLocale_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> i18nTables_;

    // ---- QuickJS path state ----
    struct JsCondRuntime;
    void AttachQuickJS(CompiledPage page);
    void DetachQuickJS();
    void WireQuickJSConditionals();
    // Helper extracted from WireQuickJSConditionals so nested v-if (a v-if
    // declared inside another v-if's subtree, surfaced as sub.conditionals)
    // can wire its own runtime without copy-pasting the watcher body.
    // Returns the runtime it created (nullptr on compile failure) so the
    // PARENT conditional can track it as a child and tear it down when its
    // own subtree unmounts.
    JsCondRuntime* WireConditional(const CompiledConditional& c);
    // Recursively dispose the nested conditional runtimes wired while a
    // v-if subtree was mounted. Their parentWidget/templateNode pointers
    // point INTO that subtree, so once the subtree is freed the watchers
    // must never fire again — otherwise a later state change runs
    // InsertChild/RemoveChild on freed widgets (use-after-free crash).
    void TearDownChildConds(JsCondRuntime* rt);
    void WireQuickJSLoops();
    void WireQuickJSModelWrite(Widget* target, const std::string& propName);
    void WireQuickJSEvent(Widget* target, const std::string& evName,
                           const std::string& sourceJs);
    void DispatchQuickJSResize(int width, int height);
    void ApplyBindingToWidget(Widget* w, const std::string& property,
                               const ui::expr::Value& v);

    std::unique_ptr<ui::uix::ScriptRuntime> jsRt_;
    JSValue              jsOptions_ = JS_UNDEFINED;
    JSValue              jsState_   = JS_UNDEFINED;
    std::vector<uint64_t> jsEffectIds_;
    std::vector<JSValue>  jsBindingFns_;
    std::vector<JSValue>  jsEventFns_;

    std::vector<std::unique_ptr<JsCondRuntime>> jsCondRuntimes_;

    struct JsLoopIteration;
    struct JsLoopRuntime;
    std::vector<std::unique_ptr<JsLoopRuntime>> jsLoopRuntimes_;
    void RebuildJsLoop(JsLoopRuntime& rt);
    void TearDownJsIterations(JsLoopRuntime& rt);
    void TearDownJsLoopRuntime(JsLoopRuntime& rt);
    void DestroyJsIteration(JsLoopRuntime& rt, JsLoopIteration& iter);
    std::unique_ptr<JsLoopRuntime> BuildJsLoopRuntime(const CompiledLoop* spec);
    // Variant for v-for nested inside v-if inside v-for. The new loop's
    // listFn / keyFn / per-iter closures are compiled with the OUTER
    // iteration's loopVar / indexVar prepended in `outerParamNames`, and at
    // call time those positional args come from `outerIter`.
    std::unique_ptr<JsLoopRuntime> BuildJsLoopRuntimeInScope(
        const CompiledLoop* spec,
        const std::set<std::string>& outerLocals,
        const std::vector<std::string>& outerParamNames,
        JsLoopIteration* outerIter,
        bool outerHasIdx);
    std::unique_ptr<JsLoopIteration> BuildJsIteration(
        JsLoopRuntime& rt, JSValue itemValue, JSValue idxValue, uint32_t idx);
    std::string ComputeIterationKey(JsLoopRuntime& rt, JSValue item,
                                     JSValue idx, uint32_t pos);
    void RewireIterationBindings(JsLoopRuntime& rt, JsLoopIteration& iter);
    // Hook a per-iteration event handler (closure already compiled by
    // CompileLoopHandlerClosure) onto a widget. Closure receives
    // (item[, idx], $event) when fired. Used by both BuildJsIteration's
    // top-level events and nested v-if mount events.
    void WireLoopScopeEvent(Widget* target, const std::string& evName,
                             JSValue fn, JsLoopIteration* iter, bool hasIdx);
    // Variant that also threads the runtime's optional outer-loop scope
    // (v-for inside v-if inside v-for). When `rt->outerIter` is set, the
    // handler closure is invoked with outer item/idx prepended.
    void WireLoopScopeEventEx(Widget* target, const std::string& evName,
                               JSValue fn, JsLoopIteration* iter, bool hasIdx,
                               JsLoopRuntime* rt);
    // Build a CondInIter for a v-if compiled inside a v-for iteration's
    // subtree (recursively, so v-if inside v-if inside v-for works). Owned
    // by the parent struct that calls it.
    std::unique_ptr<struct CondInIter> BuildCondInIter(
        const CompiledConditional& c,
        const std::set<std::string>& locals,
        const std::string& loopVar,
        const std::string& indexVar,
        JsLoopIteration* iterRaw,
        bool hasIdx);
    // Walks a widget subtree firing the registered onMount / onUnmount
    // hook for each widget that has both an id and a registered hook.
    void DispatchMountHooks(Widget* root);
    void DispatchUnmountHooks(Widget* root);

    struct LifecycleHook {
        std::function<void(Widget*)> onMount;
        std::function<void(Widget*)> onUnmount;
    };
    std::unordered_map<std::string, LifecycleHook> lifecycleHooks_;

    // ---- Menus ----
    /* Build 73 (L17) reactive: WireSubtreeMenus 给每个 ContextMenu 注册
     * beforeShowHook -> PopulateMenu(menu, compiled). Show 入口先 Clear()
     * 再 PopulateMenu, 按当前 JS state 重 build items.
     * locals: v-for 里嵌套 menuitem 时的 (iter, idx) 变量名 + value pairs,
     *         空 vector 表示顶层 (无 loop scope). */
    void PopulateMenu(ContextMenu* menu, const CompiledMenu& cm,
                       const std::vector<std::pair<std::string, JSValue>>& locals);
    void PopulateMenuItem(ContextMenu* menu, const CompiledMenuItem& mi,
                           const std::vector<std::pair<std::string, JSValue>>& locals);
    /* Eval bound expr against jsState_ + optional loop-scope locals.
     * 调用方 JS_FreeValue 返回值. 失败/无 JS state 返 JS_UNDEFINED. */
    JSValue EvalBoundExpr(const std::string& rawExpr,
                           const std::vector<std::pair<std::string, JSValue>>& locals);
    /* helpers: 把 EvalBoundExpr 的 JSValue 拿成具体类型. 返回时 JS_FreeValue
     * 已处理. */
    bool        EvalBool(const std::string& expr,
                          const std::vector<std::pair<std::string, JSValue>>& locals,
                          bool defaultVal);
    std::string EvalString(const std::string& expr,
                            const std::vector<std::pair<std::string, JSValue>>& locals);
    bool        EvalMenuBackdropBlur(const std::string& expr,
                                      const std::vector<std::pair<std::string, JSValue>>& locals,
                                      float& out);

    std::vector<std::shared_ptr<ContextMenu>> menus_;
    std::unordered_map<std::string, ContextMenu*> menuById_;
    /* Build 73 (L17): CompiledMenu* → ContextMenu* 映射. PopulateMenuItem
     * 碰到 mi.submenu 时通过它找到对应的持久 ContextMenu (submenu shell 在
     * WireSubtreeMenus 时已建好, 每次 parent rebuild 用同一个 sub 句柄). */
    std::unordered_map<const CompiledMenu*, ContextMenu*> compiledToMenu_;
    struct TriggerSpec {
        Widget*      triggerElement = nullptr;
        ContextMenu* menu = nullptr;
        bool         rclick = false;
    };
    std::vector<TriggerSpec> triggers_;
    uint64_t winHandle_ = 0;
    std::unordered_map<std::string, std::string> menuItemHandlers_;  // strId → @click JS method
    mutable std::unordered_map<std::string, uint64_t> menuHandleCache_;
};

}  // namespace ui::page

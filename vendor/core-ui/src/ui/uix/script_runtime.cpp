#include "script_runtime.h"

#include <cstring>
#include <utility>

namespace ui::uix {

namespace {

// C trampoline for the Proxy `get` trap.
//
// Called by the JS engine as: handler.get(target, key, receiver)
// We forward to the C++ ScriptRuntime stored on the JSContext via opaque.
JSValue ProxyGetTrap(JSContext* ctx, JSValueConst /*this_val*/,
                     int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    JSValueConst target = argv[0];
    JSValueConst key    = argv[1];

    auto* self = static_cast<ScriptRuntime*>(JS_GetContextOpaque(ctx));

    // Computed key fast-path: if this (target, key) is registered as a
    // computed, the cached/recomputed value comes from GetComputed and the
    // outer effect is tracked there — bypass plain JS_GetProperty so we
    // never read whatever (probably undefined) sits on the data object
    // for this name.
    if (self) {
        const char* keyStr = JS_ToCString(ctx, key);
        if (keyStr) {
            void* tp = JS_VALUE_GET_PTR(target);
            if (self->IsComputed(tp, keyStr)) {
                JSValue r = self->GetComputed(tp, keyStr);
                JS_FreeCString(ctx, keyStr);
                return r;
            }
            self->TrackRead(tp, keyStr);
            JS_FreeCString(ctx, keyStr);
        }
    }

    // Pass-through: return target[key].
    JSAtom atom = JS_ValueToAtom(ctx, key);
    JSValue result = JS_GetProperty(ctx, target, atom);
    JS_FreeAtom(ctx, atom);
    return result;
}

// C trampoline for the Proxy `set` trap.
//
// Called as: handler.set(target, key, value, receiver) → must return Boolean.
JSValue ProxySetTrap(JSContext* ctx, JSValueConst /*this_val*/,
                     int argc, JSValueConst* argv) {
    if (argc < 3) return JS_FALSE;
    JSValueConst target = argv[0];
    JSValueConst key    = argv[1];
    JSValueConst value  = argv[2];

    auto* self = static_cast<ScriptRuntime*>(JS_GetContextOpaque(ctx));

    // Pass-through write: target[key] = value (dup so target retains ownership).
    JSAtom atom = JS_ValueToAtom(ctx, key);
    int rc = JS_SetProperty(ctx, target, atom, JS_DupValue(ctx, value));
    JS_FreeAtom(ctx, atom);
    if (rc < 0) return JS_EXCEPTION;

    // Trigger dependent effects after the write lands.
    if (self) {
        const char* keyStr = JS_ToCString(ctx, key);
        if (keyStr) {
            self->TriggerWrite(JS_VALUE_GET_PTR(target), keyStr);
            JS_FreeCString(ctx, keyStr);
        }
    }

    return JS_TRUE;
}

}  // namespace

// ---- ScriptRuntime ----

ScriptRuntime::ScriptRuntime() {
    rt_  = JS_NewRuntime();
    ctx_ = JS_NewContext(rt_);
    JS_SetContextOpaque(ctx_, this);

    // Pre-build the trap functions so MakeReactive can reuse them.
    trapGet_ = JS_NewCFunction(ctx_, &ProxyGetTrap, "get", 3);
    trapSet_ = JS_NewCFunction(ctx_, &ProxySetTrap, "set", 4);
}

ScriptRuntime::~ScriptRuntime() {
    // Order matters: drop effect closures (which may hold JSValues via
    // user-side captures) before tearing down the runtime.
    effects_.clear();
    depMap_.clear();

    // Free computeds' refs while ctx_ is still alive.
    for (auto& kv : computeds_) {
        auto& c = *kv.second;
        if (!JS_IsUndefined(c.userFn))      JS_FreeValue(ctx_, c.userFn);
        if (!JS_IsUndefined(c.thisObj))     JS_FreeValue(ctx_, c.thisObj);
        if (!JS_IsUndefined(c.cachedValue)) JS_FreeValue(ctx_, c.cachedValue);
    }
    computeds_.clear();

    JS_FreeValue(ctx_, trapGet_);
    JS_FreeValue(ctx_, trapSet_);

    if (ctx_) JS_FreeContext(ctx_);
    if (rt_)  JS_FreeRuntime(rt_);
}

namespace {
// Pull the pending exception off the context, format into `out`, free the
// exception. Appends `.stack` if available for richer diagnostics.
void CaptureException(JSContext* ctx, std::string& out) {
    JSValue exc = JS_GetException(ctx);
    const char* msg = JS_ToCString(ctx, exc);
    out = msg ? msg : "(no message)";
    if (msg) JS_FreeCString(ctx, msg);

    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsException(stack)) {
        const char* s = JS_ToCString(ctx, stack);
        if (s) {
            out += "\n";
            out += s;
            JS_FreeCString(ctx, s);
        }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
}
}  // namespace

bool ScriptRuntime::Eval(const std::string& source, const std::string& filename) {
    JSValue v = JS_Eval(ctx_, source.c_str(), source.size(),
                        filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        CaptureException(ctx_, lastError_);
        JS_FreeValue(ctx_, v);
        return false;
    }
    JS_FreeValue(ctx_, v);
    lastError_.clear();
    return true;
}

ScriptRuntime::ModuleEvalResult
ScriptRuntime::EvalModule(const std::string& source, const std::string& filename) {
    ModuleEvalResult r;

    // 1) Compile only — produces a JS_TAG_MODULE value wrapping JSModuleDef*.
    JSValue compiled = JS_Eval(ctx_, source.c_str(), source.size(),
                               filename.c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) {
        CaptureException(ctx_, lastError_);
        return r;
    }

    // Stash the JSModuleDef* before EvalFunction consumes the value.
    JSModuleDef* mod = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));

    // 2) Evaluate the module's top-level body. JS_EvalFunction takes
    //    ownership of `compiled` (don't free it ourselves).
    JSValue evalRet = JS_EvalFunction(ctx_, compiled);
    if (JS_IsException(evalRet)) {
        CaptureException(ctx_, lastError_);
        return r;
    }
    JS_FreeValue(ctx_, evalRet);

    // 3) Extract the `default` export from the module's namespace.
    JSValue ns = JS_GetModuleNamespace(ctx_, mod);
    if (JS_IsException(ns)) {
        CaptureException(ctx_, lastError_);
        return r;
    }

    JSValue def = JS_GetPropertyStr(ctx_, ns, "default");
    JS_FreeValue(ctx_, ns);

    if (JS_IsException(def)) {
        CaptureException(ctx_, lastError_);
        return r;
    }
    if (JS_IsUndefined(def)) {
        lastError_ = "<script>: missing `export default` (Vue 3 Options API "
                     "requires component options to be the default export)";
        return r;
    }

    r.defaultExport = def;
    r.ok = true;
    lastError_.clear();
    return r;
}

JSValue ScriptRuntime::CompileBindingClosure(const std::string& jsExpr,
                                              const std::string& filename) {
    // Wrap so that JS_Eval returns the function value directly.
    std::string src;
    src.reserve(jsExpr.size() + 32);
    src += "(function(){return (";
    src += jsExpr;
    src += ");})";
    JSValue v = JS_Eval(ctx_, src.c_str(), src.size(),
                        filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        CaptureException(ctx_, lastError_);
    } else {
        lastError_.clear();
    }
    return v;
}

JSValue ScriptRuntime::CompileHandlerClosure(const std::string& jsExpr,
                                              const std::string& filename) {
    // Vue 3 event handler semantics:
    //   `@click="inc"`           → bare ident → auto-call inc($event)
    //   `@click="inc()"`         → already a call, ignore $event
    //   `@click="count = count + 1"` → assignment, no auto-call
    // The wrapper evaluates EXPR; if the result is a function, calls it
    // with $event. Statement-shaped EXPR (assignment) is OK in expression
    // position — the result is the assigned value, not a function.
    //
    // Use __r.call(this, $event) so bare-ident form (post-rewrite EXPR is
    // `this.inc`) keeps `this` bound to the reactive state proxy. Plain
    // `__r($event)` would lose that binding and a `this.count++` body would
    // throw in strict mode / clobber globals in sloppy.
    std::string src;
    src.reserve(jsExpr.size() + 96);
    src += "(function($event){var __r=(";
    src += jsExpr;
    src += ");if(typeof __r==='function')__r.call(this,$event);})";
    JSValue v = JS_Eval(ctx_, src.c_str(), src.size(),
                        filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        CaptureException(ctx_, lastError_);
    } else {
        lastError_.clear();
    }
    return v;
}

JSValue ScriptRuntime::CompileLoopBindingClosure(const std::string& jsExpr,
                                                  const std::string& loopVar,
                                                  const std::string& indexVar,
                                                  const std::string& filename) {
    std::string params = loopVar;
    if (!indexVar.empty()) { params += ","; params += indexVar; }

    std::string src;
    src.reserve(jsExpr.size() + params.size() + 32);
    src += "(function(";
    src += params;
    src += "){return (";
    src += jsExpr;
    src += ");})";
    JSValue v = JS_Eval(ctx_, src.c_str(), src.size(),
                        filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        CaptureException(ctx_, lastError_);
    } else {
        lastError_.clear();
    }
    return v;
}

JSValue ScriptRuntime::CompileLoopHandlerClosure(const std::string& jsExpr,
                                                  const std::string& loopVar,
                                                  const std::string& indexVar,
                                                  const std::string& filename) {
    std::string params = loopVar;
    if (!indexVar.empty()) { params += ","; params += indexVar; }
    params += ",$event";

    std::string src;
    src.reserve(jsExpr.size() + params.size() + 96);
    src += "(function(";
    src += params;
    src += "){var __r=(";
    src += jsExpr;
    src += ");if(typeof __r==='function')__r.call(this,$event);})";
    JSValue v = JS_Eval(ctx_, src.c_str(), src.size(),
                        filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        CaptureException(ctx_, lastError_);
    } else {
        lastError_.clear();
    }
    return v;
}

JSValue ScriptRuntime::CompileLoopBindingClosure(const std::string& jsExpr,
                                                  const std::vector<std::string>& paramNames,
                                                  const std::string& filename) {
    std::string params;
    for (size_t i = 0; i < paramNames.size(); ++i) {
        if (paramNames[i].empty()) continue;
        if (!params.empty()) params += ",";
        params += paramNames[i];
    }
    std::string src;
    src.reserve(jsExpr.size() + params.size() + 32);
    src += "(function(";
    src += params;
    src += "){return (";
    src += jsExpr;
    src += ");})";
    JSValue v = JS_Eval(ctx_, src.c_str(), src.size(),
                        filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) CaptureException(ctx_, lastError_);
    else lastError_.clear();
    return v;
}

JSValue ScriptRuntime::CompileLoopHandlerClosure(const std::string& jsExpr,
                                                  const std::vector<std::string>& paramNames,
                                                  const std::string& filename) {
    std::string params;
    for (size_t i = 0; i < paramNames.size(); ++i) {
        if (paramNames[i].empty()) continue;
        if (!params.empty()) params += ",";
        params += paramNames[i];
    }
    if (!params.empty()) params += ",";
    params += "$event";

    std::string src;
    src.reserve(jsExpr.size() + params.size() + 96);
    src += "(function(";
    src += params;
    src += "){var __r=(";
    src += jsExpr;
    src += ");if(typeof __r==='function')__r.call(this,$event);})";
    JSValue v = JS_Eval(ctx_, src.c_str(), src.size(),
                        filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) CaptureException(ctx_, lastError_);
    else lastError_.clear();
    return v;
}

JSValue ScriptRuntime::MakeReactive(JSValueConst target) {
    // Build handler = { get: trapGet_, set: trapSet_ } — but Proxy() consumes
    // its handler arg, so we DupValue the per-runtime trap closures.
    JSValue handler = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, handler, "get", JS_DupValue(ctx_, trapGet_));
    JS_SetPropertyStr(ctx_, handler, "set", JS_DupValue(ctx_, trapSet_));

    // new Proxy(target, handler)
    JSValue global    = JS_GetGlobalObject(ctx_);
    JSValue proxyCtor = JS_GetPropertyStr(ctx_, global, "Proxy");

    JSValue args[2] = { JS_DupValue(ctx_, target), handler };
    JSValue proxy   = JS_CallConstructor(ctx_, proxyCtor, 2, args);

    JS_FreeValue(ctx_, args[0]);  // ctor took its own ref
    JS_FreeValue(ctx_, args[1]);  // ctor took its own ref
    JS_FreeValue(ctx_, proxyCtor);
    JS_FreeValue(ctx_, global);

    return proxy;
}

uint64_t ScriptRuntime::WatchEffect(Effect fn) {
    uint64_t id = nextEffectId_++;
    effects_.emplace(id, std::move(fn));

    uint64_t prev = activeEffectId_;
    activeEffectId_ = id;
    auto it = effects_.find(id);
    if (it != effects_.end()) {
        it->second();   // initial run; collects deps via trap callbacks
    }
    activeEffectId_ = prev;

    return id;
}

void ScriptRuntime::DisposeEffect(uint64_t id) {
    effects_.erase(id);
    // Strip from every dep set (linear in dep count; fine for v0).
    for (auto it = depMap_.begin(); it != depMap_.end(); ) {
        it->second.erase(id);
        if (it->second.empty()) it = depMap_.erase(it);
        else ++it;
    }
}

void ScriptRuntime::TrackRead(void* target_ptr, const std::string& key) {
    if (activeEffectId_ == 0) return;
    depMap_[DepKey{target_ptr, key}].insert(activeEffectId_);
}

void ScriptRuntime::TriggerWrite(void* target_ptr, const std::string& key) {
    auto it = depMap_.find(DepKey{target_ptr, key});
    if (it == depMap_.end()) return;

    // Snapshot the effect set: re-running effects re-collects deps and may
    // mutate depMap_ underneath us.
    std::set<uint64_t> snapshot = it->second;
    for (uint64_t id : snapshot) {
        auto fnIt = effects_.find(id);
        if (fnIt == effects_.end()) continue;

        // Save/restore activeEffectId_ so re-runs nested under another
        // tracking effect don't get cross-contaminated.
        uint64_t prev = activeEffectId_;
        activeEffectId_ = id;
        fnIt->second();
        activeEffectId_ = prev;
    }
}

bool ScriptRuntime::IsComputed(void* target_ptr, const std::string& key) const {
    return computeds_.find(DepKey{target_ptr, key}) != computeds_.end();
}

uint64_t ScriptRuntime::DefineComputed(void* target_ptr, const std::string& key,
                                        JSValueConst userFn, JSValueConst thisObj) {
    auto ref = std::make_unique<ComputedRef>();
    ref->userFn  = JS_DupValue(ctx_, userFn);
    ref->thisObj = JS_DupValue(ctx_, thisObj);
    ref->target  = target_ptr;
    ref->key     = key;
    ref->dirty   = true;

    // Allocate the watcher effect id directly. We don't go through
    // WatchEffect because its "run fn once with tracking" contract would
    // call our scheduler body (mark-dirty + trigger) before any deps were
    // collected. Instead we register the effect entry then run userFn
    // once manually with activeEffectId_=watcherId so the user's reads
    // through the proxy land in depMap_ keyed against this watcher.
    ref->watcherId = nextEffectId_++;
    ComputedRef* refPtr = ref.get();
    effects_.emplace(ref->watcherId, [this, refPtr]() {
        // A dep changed → invalidate the cache and notify every effect
        // that depends on this computed. We do NOT recompute here; that
        // happens lazily on next GetComputed call.
        if (!refPtr->dirty) {
            refPtr->dirty = true;
            TriggerWrite(refPtr->target, refPtr->key);
        }
    });

    // Initial dep collection + cache seed.
    uint64_t prev = activeEffectId_;
    activeEffectId_ = ref->watcherId;
    JSValue v = JS_Call(ctx_, ref->userFn, ref->thisObj, 0, nullptr);
    activeEffectId_ = prev;

    if (JS_IsException(v)) {
        // Capture the message so callers see it via lastError_; cache stays
        // undefined and dirty so the next read re-attempts (in case the
        // failure was transient — e.g. depended on a key not yet defined).
        CaptureException(ctx_, lastError_);
        ref->cachedValue = JS_UNDEFINED;
        ref->dirty       = true;
    } else {
        ref->cachedValue = v;
        ref->dirty       = false;
    }

    uint64_t wid = ref->watcherId;
    computeds_.emplace(DepKey{target_ptr, key}, std::move(ref));
    return wid;
}

JSValue ScriptRuntime::GetComputed(void* target_ptr, const std::string& key) {
    auto it = computeds_.find(DepKey{target_ptr, key});
    if (it == computeds_.end()) return JS_UNDEFINED;
    auto& ref = *it->second;

    if (ref.dirty) {
        // Recompute under the watcher's effect id so reads register
        // against the watcher (not whichever outer effect is currently
        // active). Without this swap, a binding that reads the computed
        // would get its id sprayed onto every dep the user fn touches —
        // doubling re-runs and breaking computed-of-computed semantics.
        uint64_t prev = activeEffectId_;
        activeEffectId_ = ref.watcherId;
        JSValue old = ref.cachedValue;
        JSValue v   = JS_Call(ctx_, ref.userFn, ref.thisObj, 0, nullptr);
        activeEffectId_ = prev;

        if (JS_IsException(v)) {
            CaptureException(ctx_, lastError_);
            JS_FreeValue(ctx_, v);
            // Leave dirty=true so next read retries; keep prior cached value.
        } else {
            ref.cachedValue = v;
            JS_FreeValue(ctx_, old);
            ref.dirty = false;
        }
    }

    // Outer effect (if any) tracks this computed key so it re-runs when
    // the watcher fires TriggerWrite on (target_ptr, key).
    TrackRead(target_ptr, key);

    return JS_DupValue(ctx_, ref.cachedValue);
}

}  // namespace ui::uix

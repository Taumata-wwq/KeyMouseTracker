#include "value_convert.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::uix {

namespace {

// UTF-8 → UTF-16 (assumes valid UTF-8).
std::wstring Utf8ToWide(const char* s, size_t len) {
    std::wstring r;
    r.reserve(len);
    size_t i = 0;
    while (i < len) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int    take = 0;
        if      ((c & 0x80) == 0)    { cp = c;        take = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; take = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; take = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; take = 4; }
        else                          { i++; continue; }
        if (i + take > len) break;
        for (int k = 1; k < take; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += take;
        if (cp < 0x10000) {
            r += static_cast<wchar_t>(cp);
        } else {
            cp -= 0x10000;
            r += static_cast<wchar_t>(0xD800 | (cp >> 10));
            r += static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
        }
    }
    return r;
}

}  // namespace

std::wstring JSValueToWString(JSContext* ctx, JSValueConst v) {
    if (JS_IsUndefined(v) || JS_IsNull(v)) return L"";
    if (JS_IsBool(v))   return JS_ToBool(ctx, v) ? L"true" : L"false";

    if (JS_IsNumber(v)) {
        double d;
        JS_ToFloat64(ctx, &d, v);
        // Integer? print without trailing decimal — matches the legacy
        // value→string formatter for v-text bindings.
        if (std::isfinite(d) && d == std::trunc(d) &&
            d >= -9.2233720368547758e18 && d <= 9.2233720368547758e18) {
            wchar_t buf[32];
            std::swprintf(buf, 32, L"%lld", static_cast<long long>(d));
            return std::wstring(buf);
        }
        wchar_t buf[32];
        std::swprintf(buf, 32, L"%g", d);
        return std::wstring(buf);
    }

    if (JS_IsString(v)) {
        size_t len = 0;
        const char* s = JS_ToCStringLen(ctx, &len, v);
        std::wstring out = s ? Utf8ToWide(s, len) : std::wstring{};
        if (s) JS_FreeCString(ctx, s);
        return out;
    }

    // Fallback: object / array / function → JSON.stringify (handles cycles
    // by throwing; we catch and return an empty marker).
    JSValue js = JS_JSONStringify(ctx, v, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(js)) {
        JSValue exc = JS_GetException(ctx);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, js);
        return L"";
    }
    if (JS_IsString(js)) {
        size_t len = 0;
        const char* s = JS_ToCStringLen(ctx, &len, js);
        std::wstring out = s ? Utf8ToWide(s, len) : std::wstring{};
        if (s) JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, js);
        return out;
    }
    JS_FreeValue(ctx, js);
    return L"";
}

bool JSValueToBool(JSContext* ctx, JSValueConst v) {
    int rc = JS_ToBool(ctx, v);
    return rc > 0;   // -1 = error → treat as false
}

int JSValueToInt(JSContext* ctx, JSValueConst v) {
    int32_t out = 0;
    if (JS_ToInt32(ctx, &out, v) < 0) {
        // Reset pending exception (we don't propagate here)
        JSValue exc = JS_GetException(ctx);
        JS_FreeValue(ctx, exc);
        return 0;
    }
    return out;
}

double JSValueToDouble(JSContext* ctx, JSValueConst v) {
    double out = 0.0;
    if (JS_ToFloat64(ctx, &out, v) < 0) {
        JSValue exc = JS_GetException(ctx);
        JS_FreeValue(ctx, exc);
        return 0.0;
    }
    return out;
}

ui::expr::Value JSValueToExprValue(JSContext* ctx, JSValueConst v) {
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        return ui::expr::Value(nullptr);
    }
    if (JS_IsBool(v)) {
        return ui::expr::Value(JS_ToBool(ctx, v) > 0);
    }
    if (JS_IsNumber(v)) {
        double d = 0;
        JS_ToFloat64(ctx, &d, v);
        return ui::expr::Value(d);
    }
    if (JS_IsString(v)) {
        size_t len = 0;
        const char* s = JS_ToCStringLen(ctx, &len, v);
        std::string str = s ? std::string(s, len) : std::string{};
        if (s) JS_FreeCString(ctx, s);
        return ui::expr::Value(std::move(str));
    }
    if (JS_IsFunction(ctx, v)) {
        // Don't try to round-trip functions — use JS_Call for handler dispatch.
        return ui::expr::Value(nullptr);
    }
    // Array? QuickJS-NG: JS_IsArray(JSValueConst) → bool.
    if (JS_IsArray(v)) {
        ui::expr::Array out;
        JSValue lenVal = JS_GetPropertyStr(ctx, v, "length");
        uint32_t len = 0;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        out.reserve(len);
        for (uint32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, v, i);
            out.push_back(JSValueToExprValue(ctx, item));
            JS_FreeValue(ctx, item);
        }
        return ui::expr::Value::MakeArray(std::move(out));
    }
    if (JS_IsObject(v)) {
        // Iterate own enumerable string-keyed properties.
        JSPropertyEnum* tab = nullptr;
        uint32_t        len = 0;
        if (JS_GetOwnPropertyNames(ctx, &tab, &len, v,
                                    JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            JSValue exc = JS_GetException(ctx);
            JS_FreeValue(ctx, exc);
            return ui::expr::Value(nullptr);
        }
        ui::expr::Object out;
        out.reserve(len);
        for (uint32_t i = 0; i < len; ++i) {
            JSValue key = JS_AtomToString(ctx, tab[i].atom);
            const char* ks = JS_ToCString(ctx, key);
            std::string keyStr = ks ? ks : "";
            if (ks) JS_FreeCString(ctx, ks);
            JS_FreeValue(ctx, key);

            JSValue field = JS_GetProperty(ctx, v, tab[i].atom);
            out.emplace(std::move(keyStr), JSValueToExprValue(ctx, field));
            JS_FreeValue(ctx, field);
        }
        for (uint32_t i = 0; i < len; ++i) JS_FreeAtom(ctx, tab[i].atom);
        js_free(ctx, tab);
        return ui::expr::Value::MakeObject(std::move(out));
    }
    return ui::expr::Value(nullptr);
}

}  // namespace ui::uix

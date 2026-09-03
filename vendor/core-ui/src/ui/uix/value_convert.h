#pragma once
//
// value_convert — JSValue ↔ widget-property type conversion helpers.
//
// Used by the QuickJS path of PageState to plug binding results into
// the widget setters (which take std::wstring / bool / int / double).

#include "quickjs.h"
#include "../expression/value.h"

#include <string>

namespace ui::uix {

// JSValue → wide string for SetText / display purposes.
//   number     → "%g" or integer (no trailing decimal for whole numbers)
//   bool       → "true" / "false"
//   string     → UTF-8 → UTF-16
//   null/undef → ""
//   object/arr → JSON.stringify (compact)
std::wstring JSValueToWString(JSContext* ctx, JSValueConst v);

// JSValue → boolean using JS truthiness rules. (For v-if / disabled / visible.)
bool JSValueToBool(JSContext* ctx, JSValueConst v);

// JSValue → 32-bit signed integer. number truncates; string is parsed; other → 0.
int JSValueToInt(JSContext* ctx, JSValueConst v);

// JSValue → double. number direct; string parsed; bool true=1.0/false=0.0; other → 0.
double JSValueToDouble(JSContext* ctx, JSValueConst v);

// JSValue → ui::expr::Value preserving primitive type. Used to drive the
// legacy ApplyBindingToWidget(Widget*, prop, Value) which dispatches per
// (property, value type). Mappings:
//   undefined / null   → Value(nullptr)
//   boolean            → Value(bool)
//   number             → Value(double)
//   string             → Value(utf8 std::string)
//   array              → Value::MakeArray (recursive convert)
//   object             → Value::MakeObject (recursive convert; only own
//                        enumerable string keys; non-string keys skipped)
//   function           → Value(nullptr)  (functions don't round-trip; use
//                        JS_Call for handler dispatch instead)
// Recursion is bounded by the JS engine's own stack; for our template
// expressions object/array nesting is rarely deep.
ui::expr::Value JSValueToExprValue(JSContext* ctx, JSValueConst v);

}  // namespace ui::uix

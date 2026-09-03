#pragma once
//
// expr_rewriter — transforms a template expression so bare identifiers that
// refer to component state are prefixed with `this.`, matching what the
// runtime does when calling a binding closure with `this` bound to the
// reactive proxy.
//
// In Vue 3, the template compiler does the same thing: `{{ count }}` becomes
// `_ctx.count` inside the generated render function. Our story is identical
// except the binding wrapper is `function() { return EXPR; }` (called via
// JS_Call(fn, this=state, ...)), so the prefix is `this.` not `_ctx.`.
//
// Skipped (not prefixed):
//   - JS keywords / value literals: true / false / null / undefined / typeof /
//     instanceof / void / new / delete / in / of / return / let / const /
//     var / function / if / else / for / while / this / super / ...
//   - Whitelisted globals: Math / JSON / Array / Object / String / Number /
//     Boolean / Date / RegExp / Promise / Map / Set / Symbol /
//     parseInt / parseFloat / isNaN / isFinite / console / Infinity / NaN
//   - Identifier following `.` or `?.` (member-access targets)
//   - Object-literal property keys (Ident immediately followed by `:` inside
//     a `{…}` context, ternary `:` excluded via per-level `?` counting)
//   - Arrow-function parameters within their body's lexical scope.
//     Both forms recognised: `x => …` and `(a, b, c) => …`. Nested arrows
//     stack normally.
//
// Limitations (acceptable for a template-expression DSL — not full JS):
//   - Object property shorthand `{ x }` is rewritten as if `x` were a value
//     reference (becomes `{ this.x }` which is invalid JS). Write `{ x: x }`.
//   - Method shorthand `{ foo() {…} }` inside templates not handled.
//   - `let` / `const` declarations inside arrow body don't shadow `this.X`.
//   - Statements not supported (templates are expressions only).
//
// Template literals are recursed: ``Hello, ${count}!`` becomes
// ``Hello, ${this.count}!``.

#include <set>
#include <string>

namespace ui::uix {

// Rewrite `expr` so bare top-level identifiers become `this.X`. Returns the
// new string. On malformed input, output may also be malformed — we don't
// validate; downstream JS_Eval will surface the parse error.
//
// `locals` lists identifiers that should NOT be prefixed (in addition to
// the built-in keyword/global skip lists). Used by the v-for compiler to
// keep `item` / `i` as bare names so they bind to closure parameters.
std::string RewriteTemplateExpr(const std::string& expr,
                                 const std::set<std::string>& locals = {});

}  // namespace ui::uix

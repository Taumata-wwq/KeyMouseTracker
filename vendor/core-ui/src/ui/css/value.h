#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ui::css {

enum class Unit {
    None,      // pure number (no unit)
    Px,
    Em,
    Rem,
    Percent,
    Vw,
    Vh,
    Deg,       // angle for transform/gradient
    Auto,      // keyword "auto" (length context)
};

struct Length {
    double value = 0.0;
    Unit unit = Unit::Px;
    bool isAuto() const { return unit == Unit::Auto; }
};

struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;  // 0..1
    uint32_t ToArgb8888() const {
        auto clamp = [](float f) { return f < 0 ? 0.0f : (f > 1 ? 1.0f : f); };
        uint32_t A = static_cast<uint32_t>(clamp(a) * 255.0f + 0.5f);
        uint32_t R = static_cast<uint32_t>(clamp(r) * 255.0f + 0.5f);
        uint32_t G = static_cast<uint32_t>(clamp(g) * 255.0f + 0.5f);
        uint32_t B = static_cast<uint32_t>(clamp(b) * 255.0f + 0.5f);
        return (A << 24) | (R << 16) | (G << 8) | B;
    }
};

enum class ComponentKind {
    Number,    // 42
    Length,    // 8px, 1em, 50%
    Color,
    Ident,     // auto, flex, solid, center
    String,    // "hello"
    Function,  // calc(), var(), rgb() (rgb/hsl are usually pre-resolved to Color)
    Comma,
};

struct Component {
    ComponentKind kind = ComponentKind::Ident;
    double number = 0.0;           // Number
    Length length;                 // Length
    Color color;                   // Color
    std::string ident;             // Ident / String / Function name
    std::vector<Component> args;   // Function args (comma-separated top level)
};

struct ParsedValue {
    std::vector<Component> components;   // space-separated top level
    std::string error;                   // empty if ok
    bool ok = true;
};

// Parse a CSS value string into space-separated components.
// Handles: numbers, lengths, colors, identifiers, strings, function calls (calc, var, rgb, hsl, rgba, hsla), commas.
ParsedValue ParseValue(const std::string& text);

// Typed helpers for common cases.
// Accepts pure value strings like "#ff0000", "red", "rgb(255,0,0)", "rgba(0,0,0,0.5)", "hsl(0, 100%, 50%)".
bool ParseColor(const std::string& text, Color& out);

// Accepts "8px", "50%", "1.5em", "auto".
bool ParseLength(const std::string& text, Length& out);

// Named colors (subset).
bool LookupNamedColor(const std::string& name, Color& out);

// Attempt to resolve component to a single pixel-equivalent length.
// Resolution context: parentSize in pixels (for %), emSize in pixels (for em), rootEmSize (for rem),
// viewportWidth/viewportHeight (for vw/vh). Returns false if unit unsupported.
bool ResolveLengthPx(const Length& len, double parentSize, double emSize, double rootEmSize,
                     double viewportWidth, double viewportHeight, double& outPx);

// calc() evaluator. Takes a Function component (kind==Function, ident=="calc"),
// resolves its sub-expression arithmetically assuming all operands resolve to numbers
// or lengths via ResolveLengthPx under the given context.
bool EvalCalc(const Component& calc,
              double parentSize, double emSize, double rootEmSize,
              double viewportWidth, double viewportHeight,
              double& outPx);

// Resolve a var(--name) reference against a map of CSS variables.
// Returns the raw text value if found; caller re-parses it.
bool LookupVar(const std::string& name,
               const std::vector<std::pair<std::string, std::string>>& vars,
               std::string& out);

}  // namespace ui::css

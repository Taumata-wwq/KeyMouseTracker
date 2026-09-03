#include "value.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>

namespace ui::css {

// ---- Small tokenizer for value strings ----
namespace {

struct VLex {
    const std::string& s;
    size_t pos = 0;
    explicit VLex(const std::string& src) : s(src) {}
    bool Eof() const { return pos >= s.size(); }
    char Peek(size_t n = 0) const { return (pos + n < s.size()) ? s[pos + n] : '\0'; }
    void Skip() { while (!Eof() && std::isspace(static_cast<unsigned char>(Peek()))) pos++; }
    bool Starts(const char* lit) const {
        for (size_t i = 0; lit[i]; i++) if (pos + i >= s.size() || s[pos + i] != lit[i]) return false;
        return true;
    }
};

bool IsIdentStart(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) || c == '_' || c == '-';
}
bool IsIdentChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '-' || c == '_';
}

std::string ReadIdent(VLex& l) {
    std::string r;
    while (!l.Eof() && IsIdentChar(l.Peek())) { r += l.Peek(); l.pos++; }
    return r;
}

std::string ReadString(VLex& l) {
    char q = l.Peek();
    if (q != '"' && q != '\'') return "";
    l.pos++;
    std::string r;
    while (!l.Eof() && l.Peek() != q) {
        if (l.Peek() == '\\' && l.pos + 1 < l.s.size()) { l.pos++; r += l.Peek(); l.pos++; continue; }
        r += l.Peek(); l.pos++;
    }
    if (!l.Eof()) l.pos++;
    return r;
}

// Reads a numeric token with optional unit. Returns false if no number at position.
bool ReadNumberOrLength(VLex& l, Component& out) {
    size_t start = l.pos;
    if (l.Peek() == '-' || l.Peek() == '+') l.pos++;
    bool hasDigit = false;
    while (!l.Eof() && std::isdigit(static_cast<unsigned char>(l.Peek()))) { l.pos++; hasDigit = true; }
    if (l.Peek() == '.') {
        l.pos++;
        while (!l.Eof() && std::isdigit(static_cast<unsigned char>(l.Peek()))) { l.pos++; hasDigit = true; }
    }
    if (!hasDigit) { l.pos = start; return false; }
    if (l.Peek() == 'e' || l.Peek() == 'E') {
        size_t save = l.pos;
        l.pos++;
        if (l.Peek() == '+' || l.Peek() == '-') l.pos++;
        bool hasExp = false;
        while (!l.Eof() && std::isdigit(static_cast<unsigned char>(l.Peek()))) { l.pos++; hasExp = true; }
        if (!hasExp) l.pos = save;
    }
    double val = std::stod(l.s.substr(start, l.pos - start));

    // Unit / percent
    Unit unit = Unit::None;
    if (l.Peek() == '%') {
        l.pos++;
        unit = Unit::Percent;
    } else if (IsIdentStart(l.Peek())) {
        size_t uStart = l.pos;
        std::string u = ReadIdent(l);
        std::string lu;
        for (char c : u) lu += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if      (lu == "px")  unit = Unit::Px;
        else if (lu == "em")  unit = Unit::Em;
        else if (lu == "rem") unit = Unit::Rem;
        else if (lu == "vw")  unit = Unit::Vw;
        else if (lu == "vh")  unit = Unit::Vh;
        else if (lu == "deg") unit = Unit::Deg;
        else { l.pos = uStart; unit = Unit::None; }
    }

    if (unit == Unit::None) {
        out.kind = ComponentKind::Number;
        out.number = val;
    } else {
        out.kind = ComponentKind::Length;
        out.length.value = val;
        out.length.unit = unit;
    }
    return true;
}

bool ReadHexColor(VLex& l, Component& out) {
    if (l.Peek() != '#') return false;
    size_t start = l.pos;
    l.pos++;
    std::string hex;
    while (!l.Eof() && std::isxdigit(static_cast<unsigned char>(l.Peek()))) { hex += l.Peek(); l.pos++; }
    if (hex.size() == 3 || hex.size() == 4 || hex.size() == 6 || hex.size() == 8) {
        auto h2i = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        int r = 0, g = 0, b = 0, a = 255;
        if (hex.size() == 3 || hex.size() == 4) {
            int R = h2i(hex[0]), G = h2i(hex[1]), B = h2i(hex[2]);
            r = R * 16 + R; g = G * 16 + G; b = B * 16 + B;
            if (hex.size() == 4) { int A = h2i(hex[3]); a = A * 16 + A; }
        } else {
            r = h2i(hex[0]) * 16 + h2i(hex[1]);
            g = h2i(hex[2]) * 16 + h2i(hex[3]);
            b = h2i(hex[4]) * 16 + h2i(hex[5]);
            if (hex.size() == 8) a = h2i(hex[6]) * 16 + h2i(hex[7]);
        }
        out.kind = ComponentKind::Color;
        out.color.r = r / 255.0f;
        out.color.g = g / 255.0f;
        out.color.b = b / 255.0f;
        out.color.a = a / 255.0f;
        return true;
    }
    l.pos = start;
    return false;
}

bool ReadOneComponent(VLex& l, Component& out, std::string& err);

void ReadFunctionArgs(VLex& l, std::vector<Component>& args, std::string& err) {
    // l is just past '('
    while (!l.Eof()) {
        l.Skip();
        if (l.Peek() == ')') { l.pos++; return; }
        Component c;
        if (!ReadOneComponent(l, c, err)) {
            if (!err.empty()) return;
            break;
        }
        args.push_back(std::move(c));
        l.Skip();
        if (l.Peek() == ',') { Component ck; ck.kind = ComponentKind::Comma; args.push_back(ck); l.pos++; continue; }
        if (l.Peek() == ')') { l.pos++; return; }
    }
    err = "unterminated function args";
}

// rgba/hsla resolvers (convert Function to Color in-place)
bool ResolveRgbFunc(Component& func, std::string& err) {
    // args: list (optionally with commas)
    std::vector<Component> positional;
    for (auto& c : func.args) if (c.kind != ComponentKind::Comma) positional.push_back(c);
    if (positional.size() < 3 || positional.size() > 4) { err = "rgb/rgba needs 3 or 4 args"; return false; }
    auto toFloat = [](const Component& c) -> float {
        if (c.kind == ComponentKind::Number) return static_cast<float>(c.number);
        if (c.kind == ComponentKind::Length && c.length.unit == Unit::Percent) return static_cast<float>(c.length.value / 100.0 * 255.0);
        return 0.0f;
    };
    float r = std::clamp(toFloat(positional[0]) / 255.0f, 0.0f, 1.0f);
    float g = std::clamp(toFloat(positional[1]) / 255.0f, 0.0f, 1.0f);
    float b = std::clamp(toFloat(positional[2]) / 255.0f, 0.0f, 1.0f);
    float a = 1.0f;
    if (positional.size() == 4) {
        const Component& ac = positional[3];
        if (ac.kind == ComponentKind::Number) a = static_cast<float>(ac.number);
        else if (ac.kind == ComponentKind::Length && ac.length.unit == Unit::Percent) a = static_cast<float>(ac.length.value / 100.0);
    }
    Color out; out.r = r; out.g = g; out.b = b; out.a = std::clamp(a, 0.0f, 1.0f);
    func.kind = ComponentKind::Color;
    func.color = out;
    func.args.clear();
    return true;
}

float HueToRgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

bool ResolveHslFunc(Component& func, std::string& err) {
    std::vector<Component> positional;
    for (auto& c : func.args) if (c.kind != ComponentKind::Comma) positional.push_back(c);
    if (positional.size() < 3 || positional.size() > 4) { err = "hsl/hsla needs 3 or 4 args"; return false; }
    auto toNum = [](const Component& c) -> float {
        if (c.kind == ComponentKind::Number) return static_cast<float>(c.number);
        if (c.kind == ComponentKind::Length && c.length.unit == Unit::Percent) return static_cast<float>(c.length.value);
        return 0.0f;
    };
    float h = toNum(positional[0]);
    float s = toNum(positional[1]) / 100.0f;
    float l = toNum(positional[2]) / 100.0f;
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    h /= 360.0f;

    float r, g, b;
    if (s == 0.0f) {
        r = g = b = l;
    } else {
        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        r = HueToRgb(p, q, h + 1.0f / 3.0f);
        g = HueToRgb(p, q, h);
        b = HueToRgb(p, q, h - 1.0f / 3.0f);
    }
    float a = 1.0f;
    if (positional.size() == 4) a = static_cast<float>(positional[3].number);
    Color out; out.r = r; out.g = g; out.b = b; out.a = std::clamp(a, 0.0f, 1.0f);
    func.kind = ComponentKind::Color;
    func.color = out;
    func.args.clear();
    return true;
}

bool ReadOneComponent(VLex& l, Component& out, std::string& err) {
    l.Skip();
    if (l.Eof()) return false;
    char c = l.Peek();

    // String literal
    if (c == '"' || c == '\'') {
        out.kind = ComponentKind::String;
        out.ident = ReadString(l);
        return true;
    }

    // Hex color
    if (c == '#') {
        if (ReadHexColor(l, out)) return true;
    }

    // Number / length
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' ||
        ((c == '-' || c == '+') && (std::isdigit(static_cast<unsigned char>(l.Peek(1))) || l.Peek(1) == '.'))) {
        if (ReadNumberOrLength(l, out)) return true;
    }

    // Identifier (may start function)
    if (IsIdentStart(c)) {
        std::string name = ReadIdent(l);
        if (l.Peek() == '(') {
            l.pos++;
            out.kind = ComponentKind::Function;
            out.ident = name;

            std::string lname;
            for (char ch : name) lname += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            // calc() needs raw text (operators +-*/ aren't preserved by component tokenizer).
            if (lname == "calc") {
                std::string raw;
                int depth = 1;
                while (!l.Eof() && depth > 0) {
                    char ch = l.Peek();
                    if (ch == '(') depth++;
                    else if (ch == ')') { depth--; if (depth == 0) { l.pos++; break; } }
                    raw += ch;
                    l.pos++;
                }
                Component inner;
                inner.kind = ComponentKind::String;
                inner.ident = raw;
                out.args.push_back(inner);
                return true;
            }

            ReadFunctionArgs(l, out.args, err);
            if (lname == "rgb" || lname == "rgba") {
                std::string e;
                if (!ResolveRgbFunc(out, e) && err.empty()) err = e;
            } else if (lname == "hsl" || lname == "hsla") {
                std::string e;
                if (!ResolveHslFunc(out, e) && err.empty()) err = e;
            }
            return true;
        }
        // Plain ident: could be a named color
        Color named;
        if (LookupNamedColor(name, named)) {
            out.kind = ComponentKind::Color;
            out.color = named;
            return true;
        }
        out.kind = ComponentKind::Ident;
        out.ident = name;
        return true;
    }

    return false;
}

}  // namespace

ParsedValue ParseValue(const std::string& text) {
    ParsedValue r;
    VLex l(text);
    std::string err;
    while (!l.Eof()) {
        l.Skip();
        if (l.Eof()) break;
        if (l.Peek() == ',') { Component c; c.kind = ComponentKind::Comma; r.components.push_back(c); l.pos++; continue; }
        Component c;
        if (!ReadOneComponent(l, c, err)) {
            if (err.empty()) err = "unexpected character in value";
            break;
        }
        if (!err.empty()) break;
        r.components.push_back(std::move(c));
    }
    if (!err.empty()) { r.ok = false; r.error = err; }
    return r;
}

bool ParseColor(const std::string& text, Color& out) {
    auto pv = ParseValue(text);
    if (!pv.ok) return false;
    for (auto& c : pv.components) {
        if (c.kind == ComponentKind::Color) { out = c.color; return true; }
    }
    return false;
}

bool ParseLength(const std::string& text, Length& out) {
    auto pv = ParseValue(text);
    if (!pv.ok) return false;
    for (auto& c : pv.components) {
        if (c.kind == ComponentKind::Length) { out = c.length; return true; }
        if (c.kind == ComponentKind::Number) { out.value = c.number; out.unit = Unit::Px; return true; }
        if (c.kind == ComponentKind::Ident && c.ident == "auto") {
            out.value = 0; out.unit = Unit::Auto; return true;
        }
    }
    return false;
}

// ---- Named color table (subset) ----

bool LookupNamedColor(const std::string& name, Color& out) {
    static const std::unordered_map<std::string, uint32_t> table = {
        {"transparent", 0x00000000},
        {"black",       0xFF000000},
        {"white",       0xFFFFFFFF},
        {"red",         0xFFFF0000},
        {"green",       0xFF008000},
        {"blue",        0xFF0000FF},
        {"yellow",      0xFFFFFF00},
        {"cyan",        0xFF00FFFF},
        {"magenta",     0xFFFF00FF},
        {"gray",        0xFF808080},
        {"grey",        0xFF808080},
        {"silver",      0xFFC0C0C0},
        {"orange",      0xFFFFA500},
        {"purple",      0xFF800080},
        {"pink",        0xFFFFC0CB},
        {"brown",       0xFFA52A2A},
        {"navy",        0xFF000080},
        {"teal",        0xFF008080},
        {"lime",        0xFF00FF00},
        {"aqua",        0xFF00FFFF},
        {"maroon",      0xFF800000},
        {"olive",       0xFF808000},
    };
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it = table.find(lower);
    if (it == table.end()) return false;
    uint32_t v = it->second;
    out.a = ((v >> 24) & 0xFF) / 255.0f;
    out.r = ((v >> 16) & 0xFF) / 255.0f;
    out.g = ((v >> 8)  & 0xFF) / 255.0f;
    out.b = (v         & 0xFF) / 255.0f;
    return true;
}

bool ResolveLengthPx(const Length& len, double parentSize, double emSize, double rootEmSize,
                     double viewportWidth, double viewportHeight, double& outPx) {
    switch (len.unit) {
        case Unit::Px:      outPx = len.value; return true;
        case Unit::Em:      outPx = len.value * emSize; return true;
        case Unit::Rem:     outPx = len.value * rootEmSize; return true;
        case Unit::Percent: outPx = len.value / 100.0 * parentSize; return true;
        case Unit::Vw:      outPx = len.value / 100.0 * viewportWidth; return true;
        case Unit::Vh:      outPx = len.value / 100.0 * viewportHeight; return true;
        case Unit::None:    outPx = len.value; return true;  // treat unitless as px
        case Unit::Auto:    return false;
    }
    return false;
}

// ---- calc() evaluator ----
// Inner text is captured raw at parse time (because our component tokenizer
// discards operator chars). We re-tokenize here with a small dedicated lexer.
namespace {
struct CalcTok {
    enum Kind { Num, Op, LParen, RParen, End } kind = End;
    double n = 0;
    char op = 0;
};

struct CalcLex {
    std::string s;
    size_t p = 0;
    double parentSize, emSize, rootEmSize, vw, vh;
    explicit CalcLex(std::string src, double ps, double em, double rem, double w, double h)
        : s(std::move(src)), parentSize(ps), emSize(em), rootEmSize(rem), vw(w), vh(h) {}
    void Skip() { while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) p++; }
    CalcTok Next() {
        Skip();
        CalcTok t;
        if (p >= s.size()) { t.kind = CalcTok::End; return t; }
        char c = s[p];
        if (c == '(') { t.kind = CalcTok::LParen; p++; return t; }
        if (c == ')') { t.kind = CalcTok::RParen; p++; return t; }
        if (c == '+' || c == '*' || c == '/') { t.kind = CalcTok::Op; t.op = c; p++; return t; }
        if (c == '-') {
            // Could be unary or binary — parser handles it
            t.kind = CalcTok::Op; t.op = c; p++; return t;
        }
        // Read a length/number
        size_t start = p;
        bool hasDigit = false;
        while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) { p++; hasDigit = true; }
        if (p < s.size() && s[p] == '.') {
            p++;
            while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) { p++; hasDigit = true; }
        }
        if (!hasDigit) { t.kind = CalcTok::End; return t; }
        double val = std::stod(s.substr(start, p - start));
        // Unit
        Unit unit = Unit::None;
        if (p < s.size() && s[p] == '%') { unit = Unit::Percent; p++; }
        else if (p < s.size() && std::isalpha(static_cast<unsigned char>(s[p]))) {
            size_t us = p;
            while (p < s.size() && std::isalpha(static_cast<unsigned char>(s[p]))) p++;
            std::string u = s.substr(us, p - us);
            if      (u == "px") unit = Unit::Px;
            else if (u == "em") unit = Unit::Em;
            else if (u == "rem") unit = Unit::Rem;
            else if (u == "vw") unit = Unit::Vw;
            else if (u == "vh") unit = Unit::Vh;
            else { p = us; unit = Unit::None; }
        }
        Length len;
        len.value = val;
        len.unit = unit;
        double px;
        if (!ResolveLengthPx(len, parentSize, emSize, rootEmSize, vw, vh, px)) {
            t.kind = CalcTok::End;
            return t;
        }
        t.kind = CalcTok::Num;
        t.n = px;
        return t;
    }
};

struct CalcParser {
    CalcLex lex;
    CalcTok cur;
    bool ok = true;
    explicit CalcParser(CalcLex l) : lex(std::move(l)) { cur = lex.Next(); }
    void Advance() { cur = lex.Next(); }
    double ParseExpr() { return ParseAdd(); }
    double ParseAdd() {
        double l = ParseMul();
        while (cur.kind == CalcTok::Op && (cur.op == '+' || cur.op == '-')) {
            char op = cur.op; Advance();
            double r = ParseMul();
            l = (op == '+') ? l + r : l - r;
        }
        return l;
    }
    double ParseMul() {
        double l = ParseUnary();
        while (cur.kind == CalcTok::Op && (cur.op == '*' || cur.op == '/')) {
            char op = cur.op; Advance();
            double r = ParseUnary();
            l = (op == '*') ? l * r : (r == 0 ? std::nan("") : l / r);
        }
        return l;
    }
    double ParseUnary() {
        if (cur.kind == CalcTok::Op && cur.op == '-') { Advance(); return -ParseUnary(); }
        if (cur.kind == CalcTok::Op && cur.op == '+') { Advance(); return ParseUnary(); }
        return ParsePrimary();
    }
    double ParsePrimary() {
        if (cur.kind == CalcTok::Num) { double v = cur.n; Advance(); return v; }
        if (cur.kind == CalcTok::LParen) {
            Advance();
            double v = ParseAdd();
            if (cur.kind == CalcTok::RParen) Advance();
            return v;
        }
        ok = false;
        return 0.0;
    }
};

// Reconstruct raw text of a calc()'s args into a string (for the CalcLex).
// Our upstream ParseValue tokenizer already removed "+-*/" as non-idents, so we need to grab the raw
// slice of the source. We don't have the source; so we stringify the Component list as best we can.
std::string StringifyArgs(const std::vector<Component>& args) {
    std::string r;
    bool first = true;
    for (auto& c : args) {
        if (!first) r += ' ';
        first = false;
        switch (c.kind) {
            case ComponentKind::Number: {
                char buf[32]; std::snprintf(buf, sizeof(buf), "%.15g", c.number); r += buf; break;
            }
            case ComponentKind::Length: {
                char buf[32]; std::snprintf(buf, sizeof(buf), "%.15g", c.length.value); r += buf;
                switch (c.length.unit) {
                    case Unit::Px: r += "px"; break;
                    case Unit::Em: r += "em"; break;
                    case Unit::Rem: r += "rem"; break;
                    case Unit::Percent: r += "%"; break;
                    case Unit::Vw: r += "vw"; break;
                    case Unit::Vh: r += "vh"; break;
                    case Unit::None: break;
                    case Unit::Auto: break;
                }
                break;
            }
            case ComponentKind::Ident: r += c.ident; break;
            case ComponentKind::Comma: r += ","; break;
            default: break;
        }
    }
    return r;
}

}  // namespace

bool EvalCalc(const Component& calc,
              double parentSize, double emSize, double rootEmSize,
              double viewportWidth, double viewportHeight,
              double& outPx) {
    if (calc.kind != ComponentKind::Function || calc.ident != "calc") return false;
    // We stashed the raw inner text as args[0].ident (kind=String) at parse time.
    std::string raw;
    if (!calc.args.empty() && calc.args[0].kind == ComponentKind::String) {
        raw = calc.args[0].ident;
    } else {
        raw = StringifyArgs(calc.args);  // fallback
    }
    CalcLex lex(raw, parentSize, emSize, rootEmSize, viewportWidth, viewportHeight);
    CalcParser p(lex);
    outPx = p.ParseExpr();
    return p.ok;
}

bool LookupVar(const std::string& name,
               const std::vector<std::pair<std::string, std::string>>& vars,
               std::string& out) {
    for (auto& kv : vars) {
        if (kv.first == name) { out = kv.second; return true; }
    }
    return false;
}

}  // namespace ui::css

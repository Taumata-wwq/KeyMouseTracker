#include "css_parser.h"
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace ui::css {

namespace {

struct State {
    const std::string& src;
    size_t pos = 0;
    int line = 1;
    int col  = 1;
    std::vector<ParseError> errors;

    explicit State(const std::string& s) : src(s) {}
    bool Eof() const { return pos >= src.size(); }
    char Peek(size_t n = 0) const { return (pos + n < src.size()) ? src[pos + n] : '\0'; }
    bool Starts(const char* lit) const {
        for (size_t i = 0; lit[i]; ++i) {
            if (pos + i >= src.size() || src[pos + i] != lit[i]) return false;
        }
        return true;
    }
    void Advance(size_t n = 1) {
        for (size_t i = 0; i < n && pos < src.size(); ++i) {
            if (src[pos] == '\n') { line++; col = 1; }
            else col++;
            pos++;
        }
    }

    void Error(const std::string& msg) {
        ParseError e; e.message = msg; e.line = line; e.col = col;
        errors.push_back(e);
    }
    void ErrorAt(const std::string& msg, int l, int c) {
        ParseError e; e.message = msg; e.line = l; e.col = c;
        errors.push_back(e);
    }
};

void SkipWsAndComments(State& s) {
    while (!s.Eof()) {
        char c = s.Peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { s.Advance(); continue; }
        if (s.Starts("/*")) {
            s.Advance(2);
            while (!s.Eof() && !s.Starts("*/")) s.Advance();
            if (!s.Eof()) s.Advance(2);
            continue;
        }
        break;
    }
}

// Identifier chars per CSS: letters, digits, '-', '_'.
// First char cannot be digit (CSS allows -digit, but we're lax).
bool IsIdentStart(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) || c == '_' || c == '-';
}
bool IsIdentChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_' || c == '-';
}

std::string ReadIdent(State& s) {
    std::string r;
    while (!s.Eof() && IsIdentChar(s.Peek())) { r += s.Peek(); s.Advance(); }
    return r;
}

// Read a string literal (single or double quoted).
std::string ReadString(State& s) {
    char quote = s.Peek();
    if (quote != '"' && quote != '\'') return "";
    s.Advance();
    std::string r;
    while (!s.Eof() && s.Peek() != quote) {
        if (s.Peek() == '\\' && s.pos + 1 < s.src.size()) {
            s.Advance();
            r += s.Peek();
            s.Advance();
            continue;
        }
        r += s.Peek();
        s.Advance();
    }
    if (!s.Eof()) s.Advance();  // consume closing quote
    return r;
}

// ---- Selector parsing ----

SimpleSelector ParseSimpleSelector(State& s, bool& ok) {
    SimpleSelector sel;
    ok = true;
    int startLine = s.line, startCol = s.col;

    char c = s.Peek();
    if (c == '*') {
        s.Advance();
        sel.kind = SimpleKind::Universal;
        return sel;
    }
    if (c == '.') {
        s.Advance();
        if (!IsIdentStart(s.Peek())) { s.ErrorAt("expected class name after '.'", startLine, startCol); ok = false; return sel; }
        sel.kind = SimpleKind::Class;
        sel.name = ReadIdent(s);
        return sel;
    }
    if (c == '#') {
        s.Advance();
        if (!IsIdentStart(s.Peek())) { s.ErrorAt("expected id name after '#'", startLine, startCol); ok = false; return sel; }
        sel.kind = SimpleKind::Id;
        sel.name = ReadIdent(s);
        return sel;
    }
    if (c == ':') {
        s.Advance();
        // Support :: by consuming both and treating as pseudo (pseudo-elements not v0 but we don't error)
        if (s.Peek() == ':') s.Advance();
        if (!IsIdentStart(s.Peek())) { s.ErrorAt("expected pseudo name after ':'", startLine, startCol); ok = false; return sel; }
        sel.kind = SimpleKind::Pseudo;
        sel.name = ReadIdent(s);
        return sel;
    }
    if (c == '[') {
        s.Advance();
        SkipWsAndComments(s);
        if (!IsIdentStart(s.Peek())) { s.ErrorAt("expected attr name in '['", startLine, startCol); ok = false; return sel; }
        sel.kind = SimpleKind::Attr;
        sel.name = ReadIdent(s);
        SkipWsAndComments(s);
        if (s.Peek() == ']') {
            sel.attrOp = AttrOp::Exists;
            s.Advance();
            return sel;
        }
        // Operator
        if (s.Peek() == '=') { sel.attrOp = AttrOp::Equals; s.Advance(); }
        else if (s.Starts("~=")) { sel.attrOp = AttrOp::Includes; s.Advance(2); }
        else if (s.Starts("|=")) { sel.attrOp = AttrOp::DashMatch; s.Advance(2); }
        else if (s.Starts("^=")) { sel.attrOp = AttrOp::Prefix; s.Advance(2); }
        else if (s.Starts("$=")) { sel.attrOp = AttrOp::Suffix; s.Advance(2); }
        else if (s.Starts("*=")) { sel.attrOp = AttrOp::Substring; s.Advance(2); }
        else { s.ErrorAt("expected attribute operator", startLine, startCol); ok = false; return sel; }
        SkipWsAndComments(s);
        if (s.Peek() == '"' || s.Peek() == '\'') {
            sel.attrValue = ReadString(s);
        } else if (IsIdentStart(s.Peek())) {
            sel.attrValue = ReadIdent(s);
        } else {
            s.ErrorAt("expected attribute value", s.line, s.col);
            ok = false;
        }
        SkipWsAndComments(s);
        if (s.Peek() != ']') { s.ErrorAt("expected ']'", s.line, s.col); ok = false; return sel; }
        s.Advance();
        return sel;
    }
    if (IsIdentStart(c)) {
        sel.kind = SimpleKind::Tag;
        sel.name = ReadIdent(s);
        return sel;
    }
    ok = false;
    return sel;
}

// A compound selector is a sequence of simple selectors with no whitespace between them.
// Returns false if parsing failed (no selector consumed).
bool ParseCompound(State& s, CompoundSelector& out) {
    bool consumed = false;
    while (!s.Eof()) {
        char c = s.Peek();
        if (c == '*' || c == '.' || c == '#' || c == ':' || c == '[' || IsIdentStart(c)) {
            bool ok = false;
            SimpleSelector ss = ParseSimpleSelector(s, ok);
            if (!ok) return consumed;
            out.parts.push_back(ss);
            consumed = true;
            continue;
        }
        break;
    }
    return consumed;
}

bool ParseComplex(State& s, ComplexSelector& out) {
    SkipWsAndComments(s);
    CompoundSelector first;
    if (!ParseCompound(s, first)) return false;
    out.compounds.push_back(std::move(first));

    while (!s.Eof()) {
        // Capture whitespace presence
        bool ws = false;
        while (!s.Eof()) {
            char c = s.Peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ws = true; s.Advance(); }
            else if (s.Starts("/*")) { s.Advance(2); while (!s.Eof() && !s.Starts("*/")) s.Advance(); if (!s.Eof()) s.Advance(2); }
            else break;
        }
        char c = s.Peek();
        if (c == '>') {
            s.Advance();
            SkipWsAndComments(s);
            CompoundSelector next;
            if (!ParseCompound(s, next)) {
                s.Error("expected selector after '>'");
                return true;
            }
            out.combinators.push_back(Combinator::Child);
            out.compounds.push_back(std::move(next));
        } else if (ws && (c == '*' || c == '.' || c == '#' || c == ':' || c == '[' || IsIdentStart(c))) {
            CompoundSelector next;
            if (!ParseCompound(s, next)) break;
            out.combinators.push_back(Combinator::Descendant);
            out.compounds.push_back(std::move(next));
        } else {
            break;
        }
    }
    return true;
}

bool ParseSelectorList(State& s, SelectorList& out) {
    ComplexSelector first;
    SkipWsAndComments(s);
    if (!ParseComplex(s, first)) return false;
    out.push_back(std::move(first));

    while (!s.Eof()) {
        SkipWsAndComments(s);
        if (s.Peek() != ',') break;
        s.Advance();
        SkipWsAndComments(s);
        ComplexSelector next;
        if (!ParseComplex(s, next)) { s.Error("expected selector after ','"); return !out.empty(); }
        out.push_back(std::move(next));
    }
    return true;
}

// ---- Declaration parsing ----

std::string ReadValueText(State& s) {
    std::string r;
    int parenDepth = 0;
    while (!s.Eof()) {
        char c = s.Peek();
        if (c == '"' || c == '\'') {
            std::string lit = ReadString(s);
            r += '"';
            r += lit;
            r += '"';
            continue;
        }
        if (c == '(') parenDepth++;
        if (c == ')') parenDepth--;
        if ((c == ';' || c == '}') && parenDepth == 0) break;
        // comment skip
        if (s.Starts("/*")) {
            s.Advance(2);
            while (!s.Eof() && !s.Starts("*/")) s.Advance();
            if (!s.Eof()) s.Advance(2);
            continue;
        }
        r += c;
        s.Advance();
    }
    // Trim
    size_t a = r.find_first_not_of(" \t\r\n");
    size_t b = r.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return r.substr(a, b - a + 1);
}

void ParseDeclarations(State& s, std::vector<Declaration>& out) {
    while (!s.Eof()) {
        SkipWsAndComments(s);
        if (s.Peek() == '}') return;
        if (s.Eof()) return;

        int dLine = s.line, dCol = s.col;
        if (!IsIdentStart(s.Peek()) && s.Peek() != '-') {
            s.Error("expected property name");
            // Recover: skip to next ; or }
            while (!s.Eof() && s.Peek() != ';' && s.Peek() != '}') s.Advance();
            if (s.Peek() == ';') s.Advance();
            continue;
        }
        std::string prop = ReadIdent(s);
        SkipWsAndComments(s);
        if (s.Peek() != ':') {
            s.ErrorAt("expected ':' after property '" + prop + "'", s.line, s.col);
            while (!s.Eof() && s.Peek() != ';' && s.Peek() != '}') s.Advance();
            if (s.Peek() == ';') s.Advance();
            continue;
        }
        s.Advance();
        SkipWsAndComments(s);
        std::string value = ReadValueText(s);
        if (s.Peek() == ';') s.Advance();

        Declaration d;
        d.property = std::move(prop);
        d.value = std::move(value);
        d.line = dLine;
        d.col = dCol;
        out.push_back(std::move(d));
    }
}

// Skip an @-rule (e.g. @media, @import) — v0 doesn't honor them but won't error out.
void SkipAtRule(State& s) {
    s.Advance();  // consume '@'
    // Read the rule name
    while (!s.Eof() && IsIdentChar(s.Peek())) s.Advance();
    // Skip until ; or {...}
    int depth = 0;
    while (!s.Eof()) {
        char c = s.Peek();
        if (c == ';' && depth == 0) { s.Advance(); return; }
        if (c == '{') { depth++; s.Advance(); continue; }
        if (c == '}') { depth--; s.Advance(); if (depth <= 0) return; continue; }
        s.Advance();
    }
}

void ParseRule(State& s, Stylesheet& ss) {
    int startLine = s.line, startCol = s.col;
    SelectorList sels;
    if (!ParseSelectorList(s, sels)) {
        s.Error("expected selector");
        // Recover: skip to next '}' or end
        while (!s.Eof() && s.Peek() != '}') s.Advance();
        if (!s.Eof()) s.Advance();
        return;
    }
    SkipWsAndComments(s);
    if (s.Peek() != '{') {
        s.Error("expected '{' after selector");
        while (!s.Eof() && s.Peek() != '}' && s.Peek() != '{') s.Advance();
        if (s.Peek() == '{') s.Advance();
        // drain block
        int depth = 1;
        while (!s.Eof() && depth > 0) {
            if (s.Peek() == '{') depth++;
            else if (s.Peek() == '}') depth--;
            s.Advance();
        }
        return;
    }
    s.Advance();  // '{'
    Rule rule;
    rule.selectors = std::move(sels);
    rule.line = startLine;
    rule.col  = startCol;
    ParseDeclarations(s, rule.declarations);
    SkipWsAndComments(s);
    if (s.Peek() == '}') s.Advance();
    ss.rules.push_back(std::move(rule));
}

}  // namespace

ParseResult ParseStylesheet(const std::string& source) {
    ParseResult result;
    State s(source);
    while (!s.Eof()) {
        SkipWsAndComments(s);
        if (s.Eof()) break;
        if (s.Peek() == '@') {
            SkipAtRule(s);
            continue;
        }
        ParseRule(s, result.stylesheet);
    }
    result.errors = std::move(s.errors);
    result.ok = result.errors.empty();
    return result;
}

SelectorList ParseSelectorListString(const std::string& source, ParseError& err) {
    State s(source);
    SelectorList out;
    ParseSelectorList(s, out);
    if (!s.errors.empty()) {
        err = s.errors.front();
    }
    return out;
}

std::string FormatError(const ParseError& e) {
    std::ostringstream os;
    os << "line " << e.line << ":" << e.col << ": " << e.message;
    return os.str();
}

// ---- Specificity ----
int Specificity(const ComplexSelector& sel) {
    int id = 0, cls = 0, tag = 0;
    for (const auto& cmpd : sel.compounds) {
        for (const auto& sp : cmpd.parts) {
            switch (sp.kind) {
                case SimpleKind::Id:    id++; break;
                case SimpleKind::Class:
                case SimpleKind::Attr:
                case SimpleKind::Pseudo: cls++; break;
                case SimpleKind::Tag:   tag++; break;
                case SimpleKind::Universal: break;
            }
        }
    }
    return id * 10000 + cls * 100 + tag;
}

std::vector<Declaration> ParseInlineStyle(const std::string& source) {
    std::vector<Declaration> out;
    if (source.find_first_not_of(" \t\r\n") == std::string::npos) return out;
    State s(source);
    ParseDeclarations(s, out);
    return out;
}

}  // namespace ui::css

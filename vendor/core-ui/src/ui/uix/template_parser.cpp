#include "template_parser.h"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>

namespace ui::uix {

namespace {

// ---- Entity decoding ----
// Supports: &lt; &gt; &amp; &quot; &apos; &nbsp; and numeric &#NNNN; / &#xHHHH;
// Any unrecognized entity is left verbatim.
void DecodeEntity(const std::string& src, size_t& pos, std::string& out) {
    size_t start = pos;  // pos points to '&'
    size_t n = src.size();
    if (pos + 1 >= n) { out += '&'; pos++; return; }

    // Find ';' within a short window
    size_t semi = std::string::npos;
    for (size_t i = pos + 1; i < n && i - pos < 12; ++i) {
        if (src[i] == ';') { semi = i; break; }
        if (!std::isalnum(static_cast<unsigned char>(src[i])) && src[i] != '#') break;
    }
    if (semi == std::string::npos) {
        out += '&';
        pos++;
        return;
    }
    std::string ent = src.substr(pos + 1, semi - pos - 1);

    auto utf8_encode = [&](unsigned int cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6)  & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    };

    if (!ent.empty() && ent[0] == '#') {
        // Numeric
        try {
            unsigned long cp = 0;
            if (ent.size() >= 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                cp = std::stoul(ent.substr(2), nullptr, 16);
            } else {
                cp = std::stoul(ent.substr(1), nullptr, 10);
            }
            utf8_encode(static_cast<unsigned int>(cp));
            pos = semi + 1;
            return;
        } catch (...) {
            // Fall through: leave verbatim
        }
    } else if (ent == "lt")   { out += '<'; pos = semi + 1; return; }
      else if (ent == "gt")   { out += '>'; pos = semi + 1; return; }
      else if (ent == "amp")  { out += '&'; pos = semi + 1; return; }
      else if (ent == "quot") { out += '"'; pos = semi + 1; return; }
      else if (ent == "apos") { out += '\''; pos = semi + 1; return; }
      else if (ent == "nbsp") { utf8_encode(0x00A0); pos = semi + 1; return; }

    // Unrecognized: leave verbatim
    out += src.substr(start, semi - start + 1);
    pos = semi + 1;
}

struct ParserState {
    const std::string& src;
    size_t pos = 0;
    int line = 1;
    int col  = 1;
    std::vector<ParseError> errors;
    bool aborted = false;

    explicit ParserState(const std::string& s) : src(s) {}

    bool IsEof() const { return pos >= src.size() || aborted; }
    char Peek(size_t n = 0) const {
        return (pos + n < src.size()) ? src[pos + n] : '\0';
    }
    bool Starts(const char* lit) const {
        size_t i = 0;
        while (lit[i]) {
            if (pos + i >= src.size() || src[pos + i] != lit[i]) return false;
            i++;
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
    void SkipWhitespace() {
        while (!IsEof()) {
            char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') Advance();
            else break;
        }
    }
    void Error(const std::string& msg, int ln, int cl) {
        ParseError e; e.message = msg; e.line = ln; e.col = cl;
        errors.push_back(e);
    }
    void FatalError(const std::string& msg) {
        Error(msg, line, col);
        aborted = true;
    }
};

// Read tag name (stops at whitespace, /, >)
std::string ReadTagName(ParserState& p) {
    std::string name;
    while (!p.IsEof()) {
        char c = p.Peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            name += c;
            p.Advance();
        } else break;
    }
    return name;
}

// Read attribute name (can include : @ v- prefix, and . ignored at name tail)
std::string ReadAttrName(ParserState& p) {
    std::string name;
    // Prefix char (: @ allowed)
    if (p.Peek() == ':' || p.Peek() == '@') {
        name += p.Peek();
        p.Advance();
    }
    while (!p.IsEof()) {
        char c = p.Peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ':') {
            name += c;
            p.Advance();
        } else break;
    }
    return name;
}

void SkipComment(ParserState& p) {
    // Assumes we're at <!--
    p.Advance(4);
    while (!p.IsEof()) {
        if (p.Starts("-->")) { p.Advance(3); return; }
        p.Advance();
    }
    p.FatalError("unterminated comment");
}

void SkipDoctypeOrPI(ParserState& p) {
    // Skip <!DOCTYPE...> or <?xml ...?>
    p.Advance(1);  // '<'
    while (!p.IsEof() && p.Peek() != '>') p.Advance();
    if (!p.IsEof()) p.Advance();  // '>'
}

// Read attribute value (quoted, always required per L3 strict)
std::string ReadQuotedValue(ParserState& p) {
    char quote = p.Peek();
    if (quote != '"' && quote != '\'') {
        p.FatalError("expected quoted attribute value");
        return "";
    }
    p.Advance();
    std::string val;
    while (!p.IsEof() && p.Peek() != quote) {
        if (p.Peek() == '&') {
            DecodeEntity(p.src, p.pos, val);
            // pos already advanced by DecodeEntity; fix line/col
            // (DecodeEntity doesn't track col, but entities are short; minor cost)
        } else {
            val += p.Peek();
            p.Advance();
        }
    }
    if (p.IsEof()) {
        p.FatalError("unterminated attribute value");
        return val;
    }
    p.Advance();  // consume closing quote
    return val;
}

Attr ParseAttr(ParserState& p) {
    Attr attr;
    attr.line = p.line;
    attr.col  = p.col;

    std::string rawName = ReadAttrName(p);
    if (rawName.empty()) {
        p.FatalError("expected attribute name");
        return attr;
    }

    // Classify
    if (!rawName.empty() && rawName[0] == ':') {
        attr.kind = AttrKind::Bind;
        attr.name = rawName.substr(1);
    } else if (!rawName.empty() && rawName[0] == '@') {
        attr.kind = AttrKind::Event;
        attr.name = rawName.substr(1);
    } else if (rawName.rfind("v-", 0) == 0) {
        attr.kind = AttrKind::Directive;
        attr.name = rawName.substr(2);
    } else {
        attr.kind = AttrKind::Static;
        attr.name = rawName;
    }

    p.SkipWhitespace();
    if (p.Peek() == '=') {
        p.Advance();
        p.SkipWhitespace();
        attr.rawValue = ReadQuotedValue(p);
    } else {
        // No value → treat as empty string for Static, fatal for others
        if (attr.kind != AttrKind::Static) {
            p.Error("attribute '" + rawName + "' requires a value", attr.line, attr.col);
        }
    }

    // Bind / Event values are kept verbatim in attr.rawValue and handed to
    // the QuickJS path's expr_rewriter at attach time. We don't pre-parse
    // since parse errors should surface via JS_Eval (with proper stack
    // traces) rather than our hand-rolled AST.
    return attr;
}

NodePtr ParseElement(ParserState& p);
void ParseChildren(ParserState& p, std::vector<NodePtr>& out);

// Append parsed text (with entity decoding and {{ }} splitting) into children.
// Returns when it encounters '<'.
void ParseTextRun(ParserState& p, std::vector<NodePtr>& out) {
    std::string buf;
    int startLine = p.line, startCol = p.col;
    auto flushText = [&]() {
        if (buf.empty()) return;
        // Collapse runs of whitespace to a single space. PRESERVE leading/trailing
        // whitespace so text adjacent to interpolations (e.g., "Hello, {{x}}") keeps
        // its space. If the entire run is whitespace (e.g., between sibling tags),
        // drop it.
        std::string collapsed;
        collapsed.reserve(buf.size());
        bool prevWs = false;
        for (char c : buf) {
            bool ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
            if (ws) {
                if (!prevWs) collapsed += ' ';
                prevWs = true;
            } else {
                collapsed += c;
                prevWs = false;
            }
        }
        if (collapsed.empty()) {
            buf.clear();
            return;
        }
        auto node = MakeNode(NodeKind::Text, startLine, startCol);
        node->text = std::move(collapsed);
        out.push_back(std::move(node));
        buf.clear();
    };

    while (!p.IsEof()) {
        if (p.Peek() == '<') break;
        if (p.Starts("{{")) {
            flushText();
            int interpLine = p.line, interpCol = p.col;
            p.Advance(2);
            // Read until '}}'
            std::string exprSrc;
            while (!p.IsEof() && !p.Starts("}}")) {
                exprSrc += p.Peek();
                p.Advance();
            }
            if (p.IsEof()) {
                p.FatalError("unterminated interpolation");
                return;
            }
            p.Advance(2);
            auto interpNode = MakeNode(NodeKind::Interpolation, interpLine, interpCol);
            // Raw source for the QuickJS path's BuildTextSourceJs.
            interpNode->text = exprSrc;
            out.push_back(std::move(interpNode));
            startLine = p.line;
            startCol  = p.col;
            continue;
        }
        if (p.Peek() == '&') {
            DecodeEntity(p.src, p.pos, buf);
            continue;
        }
        buf += p.Peek();
        p.Advance();
    }
    flushText();
}

void ParseChildren(ParserState& p, std::vector<NodePtr>& out) {
    while (!p.IsEof()) {
        // Comment
        if (p.Starts("<!--")) { SkipComment(p); continue; }
        // Close tag → stop
        if (p.Starts("</")) return;
        // DOCTYPE / PI inside — ignore
        if (p.Starts("<!") || p.Starts("<?")) { SkipDoctypeOrPI(p); continue; }
        // Child element
        if (p.Peek() == '<') {
            NodePtr child = ParseElement(p);
            if (child) out.push_back(std::move(child));
            if (p.aborted) return;
            continue;
        }
        // Text / interpolation run
        ParseTextRun(p, out);
    }
}

NodePtr ParseElement(ParserState& p) {
    int startLine = p.line, startCol = p.col;
    if (p.Peek() != '<') {
        p.FatalError("expected '<'");
        return nullptr;
    }
    p.Advance();

    std::string tag = ReadTagName(p);
    if (tag.empty()) {
        p.FatalError("expected tag name");
        return nullptr;
    }

    auto node = MakeNode(NodeKind::Element, startLine, startCol);
    node->tag = tag;

    // Attrs
    while (!p.IsEof()) {
        p.SkipWhitespace();
        char c = p.Peek();
        if (c == '>' || c == '/' || c == '\0') break;
        Attr attr = ParseAttr(p);
        if (p.aborted) return node;
        node->attrs.push_back(std::move(attr));
    }

    // Self-close or open
    if (p.Peek() == '/') {
        p.Advance();
        if (p.Peek() != '>') {
            p.FatalError("expected '>' after '/' in self-closing tag");
            return node;
        }
        p.Advance();
        node->selfClosed = true;
        return node;
    }
    if (p.Peek() != '>') {
        p.FatalError("expected '>'");
        return node;
    }
    p.Advance();

    // Children
    ParseChildren(p, node->children);
    if (p.aborted) return node;

    // Closing tag
    if (!p.Starts("</")) {
        p.FatalError("expected closing tag for '<" + tag + ">'");
        return node;
    }
    p.Advance(2);
    std::string closeTag = ReadTagName(p);
    if (closeTag != tag) {
        p.Error("mismatched closing tag: expected '</" + tag + ">' got '</" + closeTag + ">'",
                p.line, p.col);
    }
    p.SkipWhitespace();
    if (p.Peek() != '>') {
        p.FatalError("expected '>' in closing tag");
        return node;
    }
    p.Advance();

    return node;
}

}  // namespace

ParseResult ParseTemplate(const std::string& source) {
    ParseResult result;
    ParserState p(source);

    // Skip leading whitespace, comments, doctype, PI
    while (!p.IsEof()) {
        p.SkipWhitespace();
        if (p.Starts("<!--")) { SkipComment(p); continue; }
        if (p.Starts("<!") || p.Starts("<?")) { SkipDoctypeOrPI(p); continue; }
        break;
    }

    if (p.IsEof()) {
        result.ok = false;
        ParseError e; e.message = "empty document"; e.line = 1; e.col = 1;
        result.errors.push_back(e);
        return result;
    }

    result.root = ParseElement(p);

    // Trailing whitespace/comments OK
    while (!p.IsEof()) {
        p.SkipWhitespace();
        if (p.Starts("<!--")) { SkipComment(p); continue; }
        break;
    }
    if (!p.IsEof()) {
        p.Error("unexpected content after root element", p.line, p.col);
    }

    result.errors = std::move(p.errors);
    result.ok = result.errors.empty() && result.root != nullptr;
    return result;
}

std::string FormatError(const ParseError& e) {
    std::ostringstream os;
    os << "line " << e.line << ":" << e.col << ": " << e.message;
    return os.str();
}

}  // namespace ui::uix

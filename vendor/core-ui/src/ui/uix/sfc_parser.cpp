#include "sfc_parser.h"

#include <cctype>
#include <string>

namespace ui::uix {

namespace {

struct Cursor {
    const std::string& src;
    size_t pos = 0;
    int    line = 1;
    int    col  = 1;

    explicit Cursor(const std::string& s) : src(s) {}

    bool eof() const { return pos >= src.size(); }
    char peek(size_t n = 0) const {
        return (pos + n < src.size()) ? src[pos + n] : '\0';
    }
    bool starts(const char* lit) const {
        size_t i = 0;
        while (lit[i]) {
            if (pos + i >= src.size() || src[pos + i] != lit[i]) return false;
            i++;
        }
        return true;
    }
    bool startsCI(const char* lit) const {
        size_t i = 0;
        while (lit[i]) {
            if (pos + i >= src.size()) return false;
            char a = static_cast<char>(std::tolower(static_cast<unsigned char>(src[pos + i])));
            char b = static_cast<char>(std::tolower(static_cast<unsigned char>(lit[i])));
            if (a != b) return false;
            i++;
        }
        return true;
    }
    void advance(size_t n = 1) {
        for (size_t i = 0; i < n && pos < src.size(); ++i) {
            if (src[pos] == '\n') { line++; col = 1; }
            else col++;
            pos++;
        }
    }
    void skipSpaces() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') advance();
            else break;
        }
    }
};

void pushErr(SfcDocument& doc, const std::string& msg, int ln, int cl) {
    ParseError e;
    e.message = msg;
    e.line = ln;
    e.col  = cl;
    doc.errors.push_back(std::move(e));
}

std::string readTagName(Cursor& c) {
    std::string name;
    while (!c.eof()) {
        char ch = c.peek();
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') {
            name += ch;
            c.advance();
        } else break;
    }
    return name;
}

// Read attrs up to '>' or '/>'. Returns true on success; sets `selfClosed`.
bool readAttrs(Cursor& c, std::vector<SfcAttr>& out, bool& selfClosed,
               SfcDocument& doc) {
    selfClosed = false;
    while (!c.eof()) {
        c.skipSpaces();
        char ch = c.peek();
        if (ch == '\0') {
            pushErr(doc, "unterminated tag", c.line, c.col);
            return false;
        }
        if (ch == '>') { c.advance(); return true; }
        if (ch == '/') {
            c.advance();
            c.skipSpaces();
            if (c.peek() != '>') {
                pushErr(doc, "expected '>' after '/'", c.line, c.col);
                return false;
            }
            c.advance();
            selfClosed = true;
            return true;
        }
        // Read attr name (allow : @ . to be tolerant; top-level tags don't use these
        // but we accept them so the error message is "unknown tag" instead of garbage)
        SfcAttr attr;
        int aline = c.line, acol = c.col;
        std::string name;
        if (ch == ':' || ch == '@') { name += ch; c.advance(); }
        while (!c.eof()) {
            char nc = c.peek();
            if (std::isalnum(static_cast<unsigned char>(nc)) ||
                nc == '-' || nc == '_' || nc == '.' || nc == ':') {
                name += nc;
                c.advance();
            } else break;
        }
        if (name.empty()) {
            pushErr(doc, "expected attribute name", c.line, c.col);
            return false;
        }
        attr.name = std::move(name);
        c.skipSpaces();
        if (c.peek() == '=') {
            c.advance();
            c.skipSpaces();
            char q = c.peek();
            if (q == '"' || q == '\'') {
                c.advance();
                while (!c.eof() && c.peek() != q) {
                    attr.value += c.peek();
                    c.advance();
                }
                if (c.eof()) {
                    pushErr(doc, "unterminated attribute value", aline, acol);
                    return false;
                }
                c.advance();  // consume closing quote
            } else {
                // Bare value (until whitespace, '/', or '>')
                while (!c.eof() &&
                       !std::isspace(static_cast<unsigned char>(c.peek())) &&
                       c.peek() != '/' && c.peek() != '>') {
                    attr.value += c.peek();
                    c.advance();
                }
            }
        } else {
            attr.value = "true";  // bare attr (e.g., scoped)
        }
        out.push_back(std::move(attr));
    }
    pushErr(doc, "unterminated tag", c.line, c.col);
    return false;
}

void skipComment(Cursor& c, SfcDocument& doc) {
    // Assumes we're at "<!--"
    int ln = c.line, cl = c.col;
    c.advance(4);
    while (!c.eof()) {
        if (c.starts("-->")) { c.advance(3); return; }
        c.advance();
    }
    pushErr(doc, "unterminated comment", ln, cl);
}

void skipDoctypeOrPI(Cursor& c) {
    c.advance();  // '<'
    while (!c.eof() && c.peek() != '>') c.advance();
    if (!c.eof()) c.advance();  // '>'
}

// Collect content verbatim until matching `</tagName>`. When `depthCount` is
// true, nested `<tagName ...>` opens increment depth (used for <template>
// blocks where Vue-style v-if wrappers may appear inside). Self-closing
// `<tagName .../>` does not increment depth.
std::string collectBody(Cursor& c, const std::string& tagName, bool depthCount,
                        SfcDocument& doc) {
    std::string content;
    int depth = 1;
    std::string openMarker  = "<" + tagName;
    std::string closeMarker = "</" + tagName + ">";
    int startLine = c.line, startCol = c.col;

    while (!c.eof() && depth > 0) {
        // Closing marker has priority (so <template></template> works).
        if (c.starts(closeMarker.c_str())) {
            depth--;
            if (depth == 0) {
                c.advance(closeMarker.size());
                return content;
            }
            content.append(closeMarker);
            c.advance(closeMarker.size());
            continue;
        }

        if (depthCount && c.starts(openMarker.c_str())) {
            char nextChar = c.peek(openMarker.size());
            // Only count if it's a real <tagName ...> (not <templateXyz)
            if (nextChar == ' ' || nextChar == '\t' || nextChar == '>' ||
                nextChar == '/' || nextChar == '\n' || nextChar == '\r') {
                // Look ahead to '>' to detect self-closing form (<template/>).
                // Skip over quoted attr values to avoid false `>` matches.
                size_t scan = c.pos + openMarker.size();
                bool selfClose = false;
                while (scan < c.src.size() && c.src[scan] != '>') {
                    char cc = c.src[scan];
                    if (cc == '"' || cc == '\'') {
                        char q = cc;
                        scan++;
                        while (scan < c.src.size() && c.src[scan] != q) scan++;
                        if (scan < c.src.size()) scan++;
                        continue;
                    }
                    scan++;
                }
                if (scan > c.pos + openMarker.size() && c.src[scan - 1] == '/') {
                    selfClose = true;
                }
                if (!selfClose) depth++;
            }
        }

        content += c.peek();
        c.advance();
    }
    if (depth > 0) {
        pushErr(doc, "unterminated <" + tagName + ">", startLine, startCol);
    }
    return content;
}

// Skip a non-self-closing void tag's body up to its `</tag>` closer.
// Used for <import>, <link>, and (illegal) non-self-closing <window>.
void skipUntilClose(Cursor& c, const std::string& tagName, SfcDocument& doc) {
    std::string closeMarker = "</" + tagName + ">";
    int startLine = c.line, startCol = c.col;
    while (!c.eof()) {
        if (c.starts(closeMarker.c_str())) {
            c.advance(closeMarker.size());
            return;
        }
        c.advance();
    }
    pushErr(doc, "unterminated <" + tagName + ">", startLine, startCol);
}

}  // namespace

SfcDocument ParseSfc(const std::string& source) {
    SfcDocument doc;
    Cursor c(source);

    while (!c.eof()) {
        c.skipSpaces();
        if (c.eof()) break;
        if (c.starts("<!--")) { skipComment(c, doc); continue; }
        if (c.startsCI("<!doctype") || c.starts("<?")) {
            skipDoctypeOrPI(c);
            continue;
        }
        if (c.peek() != '<') {
            pushErr(doc,
                    "unexpected content at top level (only <window>, <template>, "
                    "<script>, <style>, <import>, <link> allowed)",
                    c.line, c.col);
            doc.ok = false;
            return doc;
        }

        int tagLine = c.line, tagCol = c.col;
        c.advance();  // consume '<'
        std::string tagName = readTagName(c);
        if (tagName.empty()) {
            pushErr(doc, "expected tag name", c.line, c.col);
            doc.ok = false;
            return doc;
        }

        std::vector<SfcAttr> attrs;
        bool selfClosed = false;
        if (!readAttrs(c, attrs, selfClosed, doc)) {
            doc.ok = false;
            return doc;
        }

        if (tagName == "window") {
            if (doc.hasWindow) {
                pushErr(doc, "duplicate <window> tag", tagLine, tagCol);
            }
            doc.hasWindow   = true;
            doc.windowAttrs = std::move(attrs);
            doc.windowLine  = tagLine;
            if (!selfClosed) {
                pushErr(doc,
                        "<window> must be self-closing (use <window .../>)",
                        tagLine, tagCol);
                skipUntilClose(c, "window", doc);
            }
        } else if (tagName == "template") {
            if (doc.hasTemplate) {
                pushErr(doc, "duplicate <template> block", tagLine, tagCol);
            }
            if (selfClosed) {
                pushErr(doc, "<template> must have a body", tagLine, tagCol);
                doc.hasTemplate = true;
                continue;
            }
            doc.hasTemplate    = true;
            doc.templateLine   = c.line;
            doc.templateContent = collectBody(c, "template", true, doc);
        } else if (tagName == "script") {
            if (doc.hasScript) {
                pushErr(doc, "duplicate <script> block", tagLine, tagCol);
            }
            if (selfClosed) {
                doc.hasScript = true;
                continue;
            }
            doc.hasScript     = true;
            doc.scriptLine    = c.line;
            doc.scriptContent = collectBody(c, "script", false, doc);
        } else if (tagName == "style") {
            SfcStyleBlock blk;
            blk.line = tagLine;
            for (auto& a : attrs) {
                if (a.name == "scoped") blk.scoped = true;
            }
            if (!selfClosed) {
                blk.content = collectBody(c, "style", false, doc);
            }
            doc.styles.push_back(std::move(blk));
        } else if (tagName == "import") {
            SfcImport imp;
            imp.line = tagLine;
            for (auto& a : attrs) {
                if (a.name == "src") imp.src = a.value;
                else if (a.name == "as") imp.as = a.value;
            }
            // Derive 'as' from filename stem if missing.
            if (imp.as.empty() && !imp.src.empty()) {
                std::string stem = imp.src;
                size_t slash = stem.find_last_of("/\\");
                if (slash != std::string::npos) stem = stem.substr(slash + 1);
                size_t dot = stem.find_last_of('.');
                if (dot != std::string::npos) stem = stem.substr(0, dot);
                imp.as = stem;
            }
            if (imp.src.empty()) {
                pushErr(doc, "<import> missing 'src' attribute", tagLine, tagCol);
            } else {
                doc.imports.push_back(std::move(imp));
            }
            if (!selfClosed) skipUntilClose(c, "import", doc);
        } else if (tagName == "link") {
            SfcLink lnk;
            lnk.line = tagLine;
            for (auto& a : attrs) {
                if (a.name == "href") lnk.href = a.value;
                else if (a.name == "rel") lnk.rel = a.value;
            }
            doc.links.push_back(std::move(lnk));
            if (!selfClosed) skipUntilClose(c, "link", doc);
        } else {
            pushErr(doc,
                    "unknown top-level tag <" + tagName +
                        "> (only <window>, <template>, <script>, <style>, <import>, "
                        "<link> allowed)",
                    tagLine, tagCol);
            doc.ok = false;
            return doc;
        }
    }

    if (!doc.errors.empty()) doc.ok = false;
    return doc;
}

}  // namespace ui::uix

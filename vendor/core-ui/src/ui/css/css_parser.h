#pragma once
#include "css_ast.h"
#include <string>
#include <vector>

namespace ui::css {

struct ParseError {
    std::string message;
    int line = 0;
    int col  = 0;
};

struct ParseResult {
    Stylesheet stylesheet;
    std::vector<ParseError> errors;
    bool ok = true;
};

// Parse a CSS stylesheet (top-level rules only; @-rules currently skipped).
ParseResult ParseStylesheet(const std::string& source);

// Parse an inline style attribute body ("background:#fff;color:red") into
// declarations. Errors are silently ignored — bad inline styles just yield
// fewer decls. Returns empty vector on empty/whitespace input.
std::vector<Declaration> ParseInlineStyle(const std::string& source);

std::string FormatError(const ParseError& e);

// For tests: parse a single selector list ("a, .b, #c d")
SelectorList ParseSelectorListString(const std::string& source, ParseError& err);

}  // namespace ui::css

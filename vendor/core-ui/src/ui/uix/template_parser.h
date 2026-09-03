#pragma once
#include "template_ast.h"
#include <string>
#include <vector>

namespace ui::uix {

struct ParseError {
    std::string message;
    int line = 0;
    int col  = 0;
};

struct ParseResult {
    NodePtr root;                       // single root element
    std::vector<ParseError> errors;     // zero or more; non-empty means failure
    bool ok = true;
};

// Parse the body of a <template> block (HTML-subset DSL) into an AST.
// On success, ok == true, root is non-null.
// On failure, ok == false, errors has at least one entry; root may be partial.
ParseResult ParseTemplate(const std::string& source);

// Format an error message with line/col prefix.
std::string FormatError(const ParseError& e);

}  // namespace ui::uix

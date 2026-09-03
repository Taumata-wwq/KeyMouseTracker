#pragma once
#include "template_parser.h"   // for ParseError + FormatError
#include <string>
#include <vector>

namespace ui::uix {

// One attribute on a top-level SFC tag (e.g., on <window> or <link>).
// Names are stored without classification — top-level tags don't use
// the `:` / `@` / `v-` prefixes that the template DSL does.
struct SfcAttr {
    std::string name;
    std::string value;
};

// One <style ...>...</style> block.
struct SfcStyleBlock {
    std::string content;
    bool        scoped = false;
    int         line = 0;     // line of the opening <style ...>
};

// One <import src="..." [as="..."]/> tag.
struct SfcImport {
    std::string src;
    std::string as;           // optional; if empty, derived from filename stem
    int         line = 0;
};

// One <link rel="stylesheet" href="..."/> tag.
struct SfcLink {
    std::string href;
    std::string rel;
    int         line = 0;
};

// Parsed .uix Single-File Component.
//   - Pages have <window> + <template> (optional <script>, <style>, <import>, <link>).
//   - Components have <template> only (no <window>).
// The `script` / `template` / styles content is verbatim — downstream parsers
// (`ParseTemplate`, `ParseState`, `ParseStylesheet`) decide how to interpret it.
struct SfcDocument {
    bool                       hasWindow = false;
    std::vector<SfcAttr>       windowAttrs;
    int                        windowLine = 0;

    bool                       hasTemplate = false;
    std::string                templateContent;
    int                        templateLine = 1;     // first line of body content

    bool                       hasScript = false;
    std::string                scriptContent;
    int                        scriptLine = 1;

    std::vector<SfcStyleBlock> styles;
    std::vector<SfcImport>     imports;
    std::vector<SfcLink>       links;

    std::vector<ParseError>    errors;
    bool                       ok = true;
};

// Parse a .uix file into its top-level blocks. The recognised top-level tags are:
//   <window .../>          — page window config (self-closing only)
//   <template>...</template>  — DSL template body (verbatim)
//   <script>...</script>      — state / methods (verbatim)
//   <style [scoped]>...</style> — CSS block (verbatim, multiple allowed)
//   <import src="..." [as="..."]/>  — load a component file as a custom tag
//   <link rel="stylesheet" href="..."/>  — external CSS reference
//
// `<!-- comments -->`, `<!DOCTYPE ...>`, and `<?...?>` at the top level are skipped.
// Any other top-level content yields an error and aborts parsing.
SfcDocument ParseSfc(const std::string& source);

}  // namespace ui::uix

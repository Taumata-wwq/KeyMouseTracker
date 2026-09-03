#pragma once
#include <string>
#include <vector>

namespace ui::css {

// ---- Selectors ----

enum class SimpleKind {
    Universal,   // *
    Tag,         // div
    Class,       // .foo
    Id,          // #bar
    Attr,        // [type="text"]
    Pseudo,      // :hover
};

enum class AttrOp {
    Exists,     // [name]
    Equals,     // [name=val]
    Includes,   // [name~=val]
    DashMatch,  // [name|=val]
    Prefix,     // [name^=val]
    Suffix,     // [name$=val]
    Substring,  // [name*=val]
};

struct SimpleSelector {
    SimpleKind kind = SimpleKind::Universal;
    std::string name;         // tag / class / id / pseudo / attr name
    AttrOp attrOp = AttrOp::Exists;
    std::string attrValue;
};

enum class Combinator {
    None,         // first compound has no left combinator
    Descendant,   // "A B" (whitespace)
    Child,        // "A > B"
};

struct CompoundSelector {
    std::vector<SimpleSelector> parts;   // non-empty
};

// Complex selector: [cmpd0] [comb1] [cmpd1] [comb2] [cmpd2] ...
// combinators.size() == compounds.size() - 1
struct ComplexSelector {
    std::vector<CompoundSelector> compounds;
    std::vector<Combinator> combinators;
};

using SelectorList = std::vector<ComplexSelector>;

// ---- Declarations & Rules ----

struct Declaration {
    std::string property;
    std::string value;    // raw value text (untrimmed of surrounding ws already removed)
    int line = 0;
    int col  = 0;
};

struct Rule {
    SelectorList selectors;
    std::vector<Declaration> declarations;
    int line = 0;
    int col  = 0;
};

struct Stylesheet {
    std::vector<Rule> rules;
};

// ---- Specificity ----
//
// Encoded as a single integer (CSS standard uses (inline, id, class+attr+pseudo, tag)).
// We pack into a 32-bit int: id*10000 + (class+attr+pseudo)*100 + tag.
// Inline styles get an additional +1,000,000 bump at apply time.
int Specificity(const ComplexSelector& sel);

}  // namespace ui::css

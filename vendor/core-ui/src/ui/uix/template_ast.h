#pragma once
#include <memory>
#include <string>
#include <vector>

namespace ui::uix {

enum class NodeKind {
    Element,
    Text,           // literal text (already entity-decoded, whitespace-collapsed)
    Interpolation,  // {{ expr }} — raw expression source lives in Node::text
};

enum class AttrKind {
    Static,     // x="v"
    Bind,       // :x="expr"
    Event,      // @x="expr" (fn name or call)
    Directive,  // v-if / v-for / v-model / v-show
};

struct Attr {
    AttrKind kind = AttrKind::Static;
    std::string name;     // for Bind/Event: target ("class", "click"); for Directive: "if" / "for" / "model" / "show"
    std::string rawValue; // original string value (Bind/Event: raw JS source)
    int line = 0;
    int col  = 0;
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    NodeKind kind = NodeKind::Text;
    int line = 0;
    int col  = 0;

    // Element
    std::string tag;
    std::vector<Attr> attrs;
    std::vector<NodePtr> children;
    bool selfClosed = false;

    // Text / Interpolation: text holds either the literal text or the
    // raw JS source between {{ and }}.
    std::string text;
};

inline NodePtr MakeNode(NodeKind k, int line, int col) {
    auto n = std::make_unique<Node>();
    n->kind = k;
    n->line = line;
    n->col  = col;
    return n;
}

}  // namespace ui::uix

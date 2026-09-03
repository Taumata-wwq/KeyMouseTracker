#pragma once
#include "css_ast.h"
#include "selector.h"
#include "value.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace ui::css {

// A single resolved property value: raw declaration text (already var()-substituted where applicable).
// The applier is responsible for interpreting per property (e.g. "padding" → 1-4 lengths).
struct ResolvedDecl {
    std::string property;
    std::string value;
    int specificity = 0;
    int sourceOrder = 0;
};

// Result of cascade: property name → resolved declaration.
struct ComputedStyle {
    std::unordered_map<std::string, ResolvedDecl> decls;

    bool Has(const std::string& prop) const { return decls.find(prop) != decls.end(); }
    const std::string& Get(const std::string& prop) const {
        static const std::string empty;
        auto it = decls.find(prop);
        return it == decls.end() ? empty : it->second.value;
    }
};

// Collect and merge all rules from `sheet` that match `node`, plus inline declarations.
// Returns a ComputedStyle with ONE entry per property, the winner by specificity + source order.
// `inlineDecls` represents the element's inline `style="..."` (applied with specificity bonus).
// `cssVars` is a flat list of variable definitions from :root (already collected upstream).
ComputedStyle ComputeStyle(const Stylesheet& sheet,
                           const MatchNode& node,
                           const std::vector<Declaration>& inlineDecls,
                           const std::vector<std::pair<std::string, std::string>>& cssVars);

// Collect all :root variables from stylesheet. (Returns first set encountered.)
// Example output: [{"--accent", "#007acc"}, {"--gap", "16px"}].
std::vector<std::pair<std::string, std::string>> CollectRootVars(const Stylesheet& sheet);

// Resolve var() references in a raw value string.
// "var(--accent)" → "#007acc" if --accent is defined.
// Nested var() allowed (bounded by depth limit).
std::string ResolveVars(const std::string& raw,
                        const std::vector<std::pair<std::string, std::string>>& vars,
                        int depth = 0);

// Inherited property list: the applier uses this to know which properties walk up the parent chain
// when missing from the current computed style.
bool IsInheritedProperty(const std::string& prop);

}  // namespace ui::css

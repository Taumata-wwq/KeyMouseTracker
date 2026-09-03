#include "cascade.h"
#include <algorithm>
#include <unordered_set>

namespace ui::css {

namespace {

bool IsRootSelector(const ComplexSelector& sel) {
    if (sel.compounds.size() != 1) return false;
    const auto& c = sel.compounds[0];
    if (c.parts.size() != 1) return false;
    return c.parts[0].kind == SimpleKind::Pseudo && c.parts[0].name == "root";
}

}  // namespace

std::vector<std::pair<std::string, std::string>> CollectRootVars(const Stylesheet& sheet) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& rule : sheet.rules) {
        bool anyRoot = false;
        for (const auto& sel : rule.selectors) {
            if (IsRootSelector(sel)) { anyRoot = true; break; }
        }
        if (!anyRoot) continue;
        for (const auto& d : rule.declarations) {
            if (d.property.size() >= 2 && d.property[0] == '-' && d.property[1] == '-') {
                out.emplace_back(d.property, d.value);
            }
        }
    }
    return out;
}

std::string ResolveVars(const std::string& raw,
                        const std::vector<std::pair<std::string, std::string>>& vars,
                        int depth) {
    if (depth > 16) return raw;
    std::string out;
    size_t i = 0;
    while (i < raw.size()) {
        // Match "var("
        if (i + 4 <= raw.size() && raw.compare(i, 4, "var(") == 0) {
            size_t start = i + 4;
            int pd = 1;
            size_t j = start;
            while (j < raw.size() && pd > 0) {
                if (raw[j] == '(') pd++;
                else if (raw[j] == ')') { pd--; if (pd == 0) break; }
                j++;
            }
            if (j >= raw.size()) { out += raw.substr(i); return out; }
            std::string inner = raw.substr(start, j - start);
            // inner: "--name" or "--name, fallback"
            std::string varName, fallback;
            size_t comma = std::string::npos;
            int d2 = 0;
            for (size_t k = 0; k < inner.size(); k++) {
                if (inner[k] == '(') d2++;
                else if (inner[k] == ')') d2--;
                else if (inner[k] == ',' && d2 == 0) { comma = k; break; }
            }
            if (comma != std::string::npos) {
                varName = inner.substr(0, comma);
                fallback = inner.substr(comma + 1);
            } else {
                varName = inner;
            }
            // Trim
            auto trim = [](std::string& s) {
                size_t a = s.find_first_not_of(" \t\r\n");
                size_t b = s.find_last_not_of(" \t\r\n");
                if (a == std::string::npos) { s.clear(); return; }
                s = s.substr(a, b - a + 1);
            };
            trim(varName);
            trim(fallback);
            std::string resolved;
            if (!LookupVar(varName, vars, resolved)) {
                resolved = fallback;
            }
            out += ResolveVars(resolved, vars, depth + 1);
            i = j + 1;  // past ')'
            continue;
        }
        out += raw[i];
        i++;
    }
    return out;
}

bool IsInheritedProperty(const std::string& p) {
    static const std::unordered_set<std::string> inherited = {
        "color",
        "font-family",
        "font-size",
        "font-weight",
        "font-style",
        "line-height",
        "letter-spacing",
        "text-align",
        "text-decoration",
        "white-space",
        "word-break",
        "cursor",
        "visibility",
        "direction",
    };
    return inherited.count(p) != 0;
}

ComputedStyle ComputeStyle(const Stylesheet& sheet,
                           const MatchNode& node,
                           const std::vector<Declaration>& inlineDecls,
                           const std::vector<std::pair<std::string, std::string>>& cssVars) {
    ComputedStyle out;
    int order = 0;

    // 1. Walk rules; for matching ones, collect declarations with specificity.
    for (const auto& rule : sheet.rules) {
        // Find best matching selector in the list (they share declarations).
        int bestSpec = -1;
        for (const auto& sel : rule.selectors) {
            // Skip :root — those only define CSS vars, handled separately
            if (IsRootSelector(sel)) continue;
            if (Matches(sel, node)) {
                int s = Specificity(sel);
                if (s > bestSpec) bestSpec = s;
            }
        }
        if (bestSpec < 0) continue;

        for (const auto& d : rule.declarations) {
            // Skip custom props at non-:root level (could be valid CSS, but v0 just ignores)
            if (d.property.size() >= 2 && d.property[0] == '-' && d.property[1] == '-') continue;

            std::string resolvedValue = ResolveVars(d.value, cssVars, 0);
            auto it = out.decls.find(d.property);
            if (it == out.decls.end()) {
                ResolvedDecl rd;
                rd.property = d.property;
                rd.value = std::move(resolvedValue);
                rd.specificity = bestSpec;
                rd.sourceOrder = order++;
                out.decls[d.property] = std::move(rd);
            } else {
                // Later wins if specificity >= existing
                if (bestSpec > it->second.specificity ||
                    (bestSpec == it->second.specificity && order > it->second.sourceOrder)) {
                    it->second.value = std::move(resolvedValue);
                    it->second.specificity = bestSpec;
                    it->second.sourceOrder = order++;
                } else {
                    order++;
                }
            }
        }
    }

    // 2. Apply inline declarations (specificity bonus +1,000,000)
    for (const auto& d : inlineDecls) {
        std::string resolvedValue = ResolveVars(d.value, cssVars, 0);
        ResolvedDecl rd;
        rd.property = d.property;
        rd.value = std::move(resolvedValue);
        rd.specificity = 1000000;
        rd.sourceOrder = order++;
        out.decls[d.property] = std::move(rd);
    }

    return out;
}

}  // namespace ui::css

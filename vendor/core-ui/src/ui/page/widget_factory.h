#pragma once
#include "../widget.h"
#include "../uix/template_ast.h"
#include "../css/cascade.h"

namespace ui::page {

// Construct a Widget from an HtmlNode with its ComputedStyle applied.
// Only the current node; caller handles children (so it can wire bindings per-node).
// Text extracted from first Text child (non-interpolated). Interpolated text is left empty here
// and filled by the compiler's binding pass.
WidgetPtr BuildWidget(const ui::uix::Node& node, const ui::css::ComputedStyle& style);

// Apply common style properties (padding, margin, bg, width/height, opacity, visible) to any Widget.
void ApplyCommonStyle(Widget& w, const ui::css::ComputedStyle& style);

// Apply flex-container style properties (gap, flex-direction semantics via VBox/HBox layout).
// Applies only if the widget is a flex container.
void ApplyFlexContainerStyle(Widget& w, const ui::css::ComputedStyle& style);

// Apply flex-item style (flex-grow/shrink/basis → expanding/flex).
void ApplyFlexItemStyle(Widget& w, const ui::css::ComputedStyle& style);

}  // namespace ui::page

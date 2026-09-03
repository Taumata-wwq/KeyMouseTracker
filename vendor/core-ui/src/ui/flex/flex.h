#pragma once
#include <vector>

namespace ui::flex {

enum class Direction {
    Row,     // main axis = X (left→right)
    Column,  // main axis = Y (top→bottom)
};

enum class Justify {
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class Align {
    FlexStart,
    FlexEnd,
    Center,
    Stretch,
};

// Input per child. All values are already resolved to pixels by the caller
// (CSS %/em/rem/etc. conversion happens in the applier).
struct ChildInput {
    float basis      = 0.0f;       // flex-basis: the "hypothetical main size"
    float minMain    = 0.0f;
    float maxMain    = 1e20f;      // "infinity" sentinel
    float crossHint  = 0.0f;       // intrinsic/preferred cross size (e.g., from SizeHint)
    float minCross   = 0.0f;
    float maxCross   = 1e20f;
    float grow       = 0.0f;
    float shrink     = 1.0f;
    Align alignSelf  = Align::Stretch;  // resolved (caller inherits from container if CSS had no value)
    bool  crossIsAuto = true;      // true: stretch/auto on cross axis; false: crossHint is explicit size
};

struct ContainerInput {
    Direction direction  = Direction::Row;
    Justify   justify    = Justify::FlexStart;
    Align     alignItems = Align::Stretch;
    float     gap        = 0.0f;
    float     mainSize   = 0.0f;   // container's inner main-axis size
    float     crossSize  = 0.0f;   // container's inner cross-axis size
    std::vector<ChildInput> children;
};

struct ChildResult {
    float mainPos   = 0.0f;
    float crossPos  = 0.0f;
    float mainSize  = 0.0f;
    float crossSize = 0.0f;
};

struct LayoutResult {
    std::vector<ChildResult> children;
    float usedMain = 0.0f;   // sum of sizes + gaps actually consumed
};

// Run the flex algorithm. Pure function — no globals, no widgets.
LayoutResult Layout(const ContainerInput& in);

}  // namespace ui::flex

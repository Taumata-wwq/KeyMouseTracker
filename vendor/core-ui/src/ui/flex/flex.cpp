#include "flex.h"
#include <algorithm>

namespace ui::flex {

namespace {

float Clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Distribute positive `slack` to children weighted by grow, respecting maxMain clamps.
// Returns the actual amount distributed (may be less than slack if all children hit max).
void DistributeGrow(const ContainerInput& in,
                    const std::vector<float>& basis,
                    std::vector<float>& mainSize,
                    float slack) {
    const size_t n = in.children.size();
    if (n == 0) return;

    float totalGrow = 0.0f;
    for (auto& c : in.children) totalGrow += c.grow;
    if (totalGrow <= 0.0f) return;

    std::vector<bool> frozen(n, false);
    // Freeze children that are already at or above max
    for (size_t i = 0; i < n; ++i) {
        if (mainSize[i] >= in.children[i].maxMain) frozen[i] = true;
    }

    float remaining = slack;
    for (int iter = 0; iter < 16 && remaining > 0.1f; ++iter) {
        float activeGrow = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (!frozen[i]) activeGrow += in.children[i].grow;
        }
        if (activeGrow <= 0.0f) break;

        float nextRemaining = 0.0f;
        bool anyFroze = false;
        for (size_t i = 0; i < n; ++i) {
            if (frozen[i]) continue;
            float add = remaining * (in.children[i].grow / activeGrow);
            float target = mainSize[i] + add;
            if (target > in.children[i].maxMain) {
                nextRemaining += target - in.children[i].maxMain;
                mainSize[i] = in.children[i].maxMain;
                frozen[i] = true;
                anyFroze = true;
            } else {
                mainSize[i] = target;
            }
        }
        remaining = nextRemaining;
        if (!anyFroze) break;
    }
    (void)basis;  // basis not needed for grow
}

// Distribute negative `slack` (shrinkage needed) weighted by shrink * basis.
void DistributeShrink(const ContainerInput& in,
                      const std::vector<float>& basis,
                      std::vector<float>& mainSize,
                      float slack) {
    const size_t n = in.children.size();
    if (n == 0) return;

    std::vector<bool> frozen(n, false);
    for (size_t i = 0; i < n; ++i) {
        if (mainSize[i] <= in.children[i].minMain) frozen[i] = true;
    }

    float remaining = slack;  // negative
    for (int iter = 0; iter < 16 && remaining < -0.1f; ++iter) {
        float activeWeighted = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (!frozen[i]) activeWeighted += in.children[i].shrink * basis[i];
        }
        if (activeWeighted <= 0.0f) break;

        float nextRemaining = 0.0f;
        bool anyFroze = false;
        for (size_t i = 0; i < n; ++i) {
            if (frozen[i]) continue;
            float weight = in.children[i].shrink * basis[i];
            float delta = remaining * (weight / activeWeighted);  // negative
            float target = mainSize[i] + delta;
            if (target < in.children[i].minMain) {
                nextRemaining += target - in.children[i].minMain;  // negative contribution
                mainSize[i] = in.children[i].minMain;
                frozen[i] = true;
                anyFroze = true;
            } else {
                mainSize[i] = target;
            }
        }
        remaining = nextRemaining;
        if (!anyFroze) break;
    }
}

void PlaceMainAxis(const ContainerInput& in,
                   const std::vector<float>& mainSize,
                   LayoutResult& out) {
    const size_t n = in.children.size();
    float usedMain = 0.0f;
    for (auto s : mainSize) usedMain += s;
    float totalGap = (n > 1) ? in.gap * static_cast<float>(n - 1) : 0.0f;
    float totalUsed = usedMain + totalGap;
    float freeSpace = in.mainSize - totalUsed;
    if (freeSpace < 0.0f) freeSpace = 0.0f;

    float startOffset = 0.0f;
    float spacing = in.gap;

    switch (in.justify) {
        case Justify::FlexStart:
            startOffset = 0.0f;
            spacing = in.gap;
            break;
        case Justify::FlexEnd:
            startOffset = freeSpace;
            spacing = in.gap;
            break;
        case Justify::Center:
            startOffset = freeSpace * 0.5f;
            spacing = in.gap;
            break;
        case Justify::SpaceBetween:
            startOffset = 0.0f;
            if (n > 1) {
                spacing = in.gap + freeSpace / static_cast<float>(n - 1);
            }
            break;
        case Justify::SpaceAround:
            if (n > 0) {
                float each = freeSpace / static_cast<float>(n);
                startOffset = each * 0.5f;
                spacing = in.gap + each;
            }
            break;
        case Justify::SpaceEvenly:
            if (n > 0) {
                float each = freeSpace / static_cast<float>(n + 1);
                startOffset = each;
                spacing = in.gap + each;
            }
            break;
    }

    float cursor = startOffset;
    for (size_t i = 0; i < n; ++i) {
        out.children[i].mainPos = cursor;
        out.children[i].mainSize = mainSize[i];
        cursor += mainSize[i] + spacing;
    }
    out.usedMain = totalUsed;
}

void PlaceCrossAxis(const ContainerInput& in, LayoutResult& out) {
    const size_t n = in.children.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& c = in.children[i];
        Align align = c.alignSelf;  // caller already resolved 'auto' to container's alignItems

        float crossSize;
        if (align == Align::Stretch && c.crossIsAuto) {
            crossSize = Clamp(in.crossSize, c.minCross, c.maxCross);
        } else {
            crossSize = Clamp(c.crossHint, c.minCross, c.maxCross);
        }

        float crossPos = 0.0f;
        switch (align) {
            case Align::FlexStart: crossPos = 0.0f; break;
            case Align::FlexEnd:   crossPos = in.crossSize - crossSize; break;
            case Align::Center:    crossPos = (in.crossSize - crossSize) * 0.5f; break;
            case Align::Stretch:   crossPos = 0.0f; break;
        }

        out.children[i].crossPos = crossPos;
        out.children[i].crossSize = crossSize;
    }
}

}  // namespace

LayoutResult Layout(const ContainerInput& in) {
    LayoutResult out;
    const size_t n = in.children.size();
    out.children.resize(n);
    if (n == 0) return out;

    // Step 1: initial main size = basis, clamped to [min,max]
    std::vector<float> basis(n);
    std::vector<float> mainSize(n);
    for (size_t i = 0; i < n; ++i) {
        basis[i] = Clamp(in.children[i].basis, in.children[i].minMain, in.children[i].maxMain);
        mainSize[i] = basis[i];
    }

    // Step 2: Compute available main-axis space (minus gaps)
    float totalGap = (n > 1) ? in.gap * static_cast<float>(n - 1) : 0.0f;
    float sumBasis = 0.0f;
    for (auto b : basis) sumBasis += b;
    float available = in.mainSize - totalGap;
    float slack = available - sumBasis;

    // Step 3: Distribute grow or shrink
    if (slack > 0.1f) {
        DistributeGrow(in, basis, mainSize, slack);
    } else if (slack < -0.1f) {
        DistributeShrink(in, basis, mainSize, slack);
    }

    // Step 4: Place main axis (justify-content)
    PlaceMainAxis(in, mainSize, out);

    // Step 5: Place cross axis (align-items / align-self)
    PlaceCrossAxis(in, out);

    return out;
}

}  // namespace ui::flex

#include "animation_host.h"

#include <algorithm>
#include <utility>

namespace ui {

void AnimationHost::RegisterWidget(Widget* widget, AnimationInvalidation invalidation) {
    if (!widget) return;

    for (auto& target : targets_) {
        if (target.widget == widget) {
            target.invalidation |= invalidation;
            return;
        }
    }

    targets_.push_back(AnimationTarget{widget, invalidation});
}

void AnimationHost::RemoveWidget(Widget* widget) {
    targets_.erase(std::remove_if(targets_.begin(), targets_.end(),
                                  [widget](const AnimationTarget& target) {
                                      return target.widget == widget;
                                  }),
                   targets_.end());
}

void AnimationHost::Clear() {
    targets_.clear();
}

void AnimationHost::ReplaceTargets(std::vector<AnimationTarget> targets) {
    targets_ = std::move(targets);
}

}  // namespace ui

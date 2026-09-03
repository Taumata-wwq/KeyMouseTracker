#pragma once

#ifndef UI_API
  #if defined(UI_CORE_STATIC)
    #define UI_API
  #elif defined(UI_CORE_BUILDING)
    #define UI_API __declspec(dllexport)
  #else
    #define UI_API __declspec(dllimport)
  #endif
#endif

#include <cstdint>
#include <vector>

namespace ui {

class Widget;

enum class AnimationInvalidation : uint32_t {
    None = 0,
    Paint = 1u << 0,
    Layout = 1u << 1,
    HitTest = 1u << 2,
    NativeWindow = 1u << 3,
};

inline AnimationInvalidation operator|(AnimationInvalidation a, AnimationInvalidation b) {
    return static_cast<AnimationInvalidation>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline AnimationInvalidation& operator|=(AnimationInvalidation& a, AnimationInvalidation b) {
    a = a | b;
    return a;
}

inline bool HasAnimationInvalidation(AnimationInvalidation value, AnimationInvalidation flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

struct AnimationTarget {
    Widget* widget = nullptr;
    AnimationInvalidation invalidation = AnimationInvalidation::Paint;
};

class UI_API AnimationHost {
public:
    void RegisterWidget(Widget* widget,
                        AnimationInvalidation invalidation = AnimationInvalidation::Paint);
    void RemoveWidget(Widget* widget);
    void Clear();

    bool Empty() const { return targets_.empty(); }
    size_t Size() const { return targets_.size(); }
    const std::vector<AnimationTarget>& Targets() const { return targets_; }
    void ReplaceTargets(std::vector<AnimationTarget> targets);

private:
    std::vector<AnimationTarget> targets_;
};

}  // namespace ui

#pragma once

#include <cstdint>

namespace ui {

enum class FrameReason : uint32_t {
    None      = 0,
    Resize    = 1u << 0,
    Layout    = 1u << 1,
    Paint     = 1u << 2,
    Animation = 1u << 3,
    Tile      = 1u << 4,
    Input     = 1u << 5,
    Final     = 1u << 6,
    Startup   = 1u << 7,
    Screenshot = 1u << 8,
    VisualTransaction = 1u << 9,
};

inline FrameReason operator|(FrameReason a, FrameReason b) {
    return static_cast<FrameReason>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline FrameReason operator&(FrameReason a, FrameReason b) {
    return static_cast<FrameReason>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline FrameReason& operator|=(FrameReason& a, FrameReason b) {
    a = a | b;
    return a;
}

inline bool HasFrameReason(FrameReason reasons, FrameReason reason) {
    return (static_cast<uint32_t>(reasons & reason) != 0);
}

enum class PresentPolicy : uint8_t {
    Deferred = 0,
    Immediate = 1,
    Final = 2,
    Screenshot = 3,
};

struct FrameRequest {
    FrameReason reasons = FrameReason::None;
    PresentPolicy policy = PresentPolicy::Deferred;
    uint64_t generation = 0;
};

inline bool FrameRequiresPresentBarrier(const FrameRequest& frame) {
    return frame.generation != 0 &&
           HasFrameReason(frame.reasons, FrameReason::VisualTransaction);
}

class FrameScheduler {
public:
    void Request(FrameReason reason, PresentPolicy policy = PresentPolicy::Deferred);
    void RequestPaint(PresentPolicy policy = PresentPolicy::Deferred);
    void RequestResize(PresentPolicy policy = PresentPolicy::Immediate);
    void RequestInteractiveFrame(FrameReason reason = FrameReason::Paint);
    void RequestFinalFrame(FrameReason reason = FrameReason::Paint);
    void RequestVisualTransaction(bool resizeDirty, bool paintDirty, bool final = false);
    bool HasPending() const { return pendingReasons_ != FrameReason::None; }
    FrameReason PendingReasons() const { return pendingReasons_; }
    PresentPolicy PendingPolicy() const { return pendingPolicy_; }

    FrameRequest BeginFrame();
    void CompleteFrame(uint64_t generation);
    bool FrameInProgress() const { return inProgressGeneration_ != 0; }
    uint64_t InProgressGeneration() const { return inProgressGeneration_; }
    uint64_t LastSubmittedGeneration() const { return lastSubmittedGeneration_; }

private:
    static PresentPolicy MergePolicy(PresentPolicy current, PresentPolicy incoming);

    FrameReason pendingReasons_ = FrameReason::None;
    PresentPolicy pendingPolicy_ = PresentPolicy::Deferred;
    uint64_t nextGeneration_ = 1;
    uint64_t inProgressGeneration_ = 0;
    uint64_t lastSubmittedGeneration_ = 0;
};

} // namespace ui

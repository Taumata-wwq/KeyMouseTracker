#include "frame_scheduler.h"

namespace ui {

void FrameScheduler::Request(FrameReason reason, PresentPolicy policy) {
    if (reason == FrameReason::None) return;
    pendingReasons_ |= reason;
    pendingPolicy_ = MergePolicy(pendingPolicy_, policy);
}

void FrameScheduler::RequestPaint(PresentPolicy policy) {
    Request(FrameReason::Paint, policy);
}

void FrameScheduler::RequestResize(PresentPolicy policy) {
    Request(FrameReason::Resize, policy);
}

void FrameScheduler::RequestInteractiveFrame(FrameReason reason) {
    Request(reason | FrameReason::Input, PresentPolicy::Immediate);
}

void FrameScheduler::RequestFinalFrame(FrameReason reason) {
    Request(reason | FrameReason::Final, PresentPolicy::Final);
}

void FrameScheduler::RequestVisualTransaction(bool resizeDirty,
                                              bool paintDirty,
                                              bool final) {
    if (!resizeDirty && !paintDirty && !final) return;
    FrameReason reason = FrameReason::VisualTransaction;
    if (resizeDirty) reason |= FrameReason::Resize;
    if (paintDirty)  reason |= FrameReason::Paint;
    if (final)       reason |= FrameReason::Final;
    Request(reason, final ? PresentPolicy::Final : PresentPolicy::Immediate);
}

FrameRequest FrameScheduler::BeginFrame() {
    FrameRequest request;
    request.reasons = pendingReasons_;
    request.policy = pendingPolicy_;
    if (request.reasons == FrameReason::None) {
        return request;
    }

    request.generation = nextGeneration_++;
    pendingReasons_ = FrameReason::None;
    pendingPolicy_ = PresentPolicy::Deferred;
    inProgressGeneration_ = request.generation;
    lastSubmittedGeneration_ = request.generation;
    return request;
}

void FrameScheduler::CompleteFrame(uint64_t generation) {
    if (generation == 0 || generation != inProgressGeneration_) return;
    inProgressGeneration_ = 0;
}

PresentPolicy FrameScheduler::MergePolicy(PresentPolicy current, PresentPolicy incoming) {
    return static_cast<uint8_t>(incoming) > static_cast<uint8_t>(current)
        ? incoming
        : current;
}

} // namespace ui

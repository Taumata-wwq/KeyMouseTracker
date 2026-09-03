#include "binding.h"
#include "property.h"
#include <algorithm>
#include <cstdio>

namespace ui::reactive {

namespace {
thread_local Binding* g_current = nullptr;
}

Binding* CurrentBinding() { return g_current; }

CaptureScope::CaptureScope(Binding* b) : prev(g_current) { g_current = b; }
CaptureScope::~CaptureScope() { g_current = prev; }

Binding::Binding(std::function<void()> fn) : fn_(std::move(fn)) {}

Binding::~Binding() { ClearDependencies(); }

void Binding::ClearDependencies() {
    for (auto* p : dependencies_) {
        p->RemoveObserver(this);
    }
    dependencies_.clear();
}

void Binding::AddDependency(PropertyBase* p) {
    if (std::find(dependencies_.begin(), dependencies_.end(), p) != dependencies_.end()) return;
    dependencies_.push_back(p);
    p->AddObserver(this);
}

void Binding::Evaluate() {
    if (evaluating_) {
        std::fprintf(stderr, "[reactive] binding loop detected; skipping re-eval\n");
        return;
    }
    evaluating_ = true;
    ClearDependencies();
    {
        CaptureScope scope(this);
        if (fn_) fn_();
    }
    dirty_ = false;
    evaluating_ = false;
}

void Binding::MarkDirty() {
    if (dirty_) return;
    dirty_ = true;
    Evaluate();
}

PropertyBase::~PropertyBase() {
    for (auto* b : observers_) {
        auto& deps = b->dependencies_;
        deps.erase(std::remove(deps.begin(), deps.end(), this), deps.end());
    }
}

void PropertyBase::AddObserver(Binding* b) {
    if (std::find(observers_.begin(), observers_.end(), b) == observers_.end()) {
        observers_.push_back(b);
    }
}

void PropertyBase::RemoveObserver(Binding* b) {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), b), observers_.end());
}

void PropertyBase::NotifyObservers() {
    if (observers_.empty()) return;
    auto snapshot = observers_;
    for (auto* b : snapshot) {
        b->MarkDirty();
    }
}

void PropertyBase::CaptureIfReading() {
    Binding* b = g_current;
    if (!b) return;
    b->AddDependency(this);
}

}  // namespace ui::reactive

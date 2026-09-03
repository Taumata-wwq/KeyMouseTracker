#pragma once
#include <functional>
#include <vector>

namespace ui::reactive {

class PropertyBase;

class Binding {
public:
    explicit Binding(std::function<void()> fn);
    ~Binding();

    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;
    Binding(Binding&&) = delete;
    Binding& operator=(Binding&&) = delete;

    void Evaluate();
    void MarkDirty();

    bool IsDirty() const { return dirty_; }
    size_t DependencyCount() const { return dependencies_.size(); }

private:
    std::function<void()> fn_;
    std::vector<PropertyBase*> dependencies_;
    bool dirty_ = true;
    bool evaluating_ = false;

    void ClearDependencies();
    void AddDependency(PropertyBase* p);

    friend class PropertyBase;
};

Binding* CurrentBinding();

struct CaptureScope {
    Binding* prev;
    explicit CaptureScope(Binding* b);
    ~CaptureScope();

    CaptureScope(const CaptureScope&) = delete;
    CaptureScope& operator=(const CaptureScope&) = delete;
};

}  // namespace ui::reactive

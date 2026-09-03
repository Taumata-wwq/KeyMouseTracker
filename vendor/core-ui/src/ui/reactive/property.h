#pragma once
#include "binding.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ui::reactive {

class PropertyBase {
public:
    virtual ~PropertyBase();

    PropertyBase(const PropertyBase&) = delete;
    PropertyBase& operator=(const PropertyBase&) = delete;
    PropertyBase(PropertyBase&&) = delete;
    PropertyBase& operator=(PropertyBase&&) = delete;

    size_t ObserverCount() const { return observers_.size(); }

protected:
    PropertyBase() = default;

    void CaptureIfReading();
    void NotifyObservers();

private:
    void AddObserver(Binding* b);
    void RemoveObserver(Binding* b);

    std::vector<Binding*> observers_;
    friend class Binding;
};

template <typename T>
class Property : public PropertyBase {
public:
    Property() = default;
    explicit Property(T v) : value_(std::move(v)) {}

    const T& Get() const {
        const_cast<Property*>(this)->CaptureIfReading();
        return value_;
    }

    void Set(T v) {
        if (value_ == v) return;
        value_ = std::move(v);
        NotifyObservers();
    }

    void SetSilent(T v) { value_ = std::move(v); }

    void Bind(std::function<T()> expr) {
        ownedBinding_.reset();
        ownedBinding_ = std::make_unique<Binding>([this, expr = std::move(expr)] {
            T result = expr();
            if (!(value_ == result)) {
                value_ = std::move(result);
                NotifyObservers();
            }
        });
        ownedBinding_->Evaluate();
    }

    void Unbind() { ownedBinding_.reset(); }
    bool IsBound() const { return ownedBinding_ != nullptr; }

private:
    T value_{};
    std::unique_ptr<Binding> ownedBinding_;
};

}  // namespace ui::reactive

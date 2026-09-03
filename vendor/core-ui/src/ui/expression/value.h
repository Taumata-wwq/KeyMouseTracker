#pragma once
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ui::expr {

class Value;
struct Node;  // forward declaration (from ast.h)

using Array = std::vector<Value>;
using Object = std::unordered_map<std::string, Value>;

// A function value (created by arrow-function expressions).
// The body Node is owned elsewhere (by the expression AST embedded in a StateEntry
// or CompiledBinding), so we hold a non-owning pointer — the Node outlives any
// transient Value since arrow functions are only created during evaluation of
// an enclosing AST.
struct FunctionData {
    std::vector<std::string> params;
    const Node* body = nullptr;
};

enum class ValueKind {
    Null,
    Number,
    Bool,
    String,
    Array,
    Object,
    Function,
};

class Value {
public:
    // ---- Construction ----
    Value() : data_(std::monostate{}) {}                      // Null
    Value(std::nullptr_t) : data_(std::monostate{}) {}
    Value(double n) : data_(n) {}
    Value(int n) : data_(static_cast<double>(n)) {}
    Value(bool b) : data_(b) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(std::shared_ptr<Array> a) : data_(std::move(a)) {}
    Value(std::shared_ptr<Object> o) : data_(std::move(o)) {}
    Value(std::shared_ptr<FunctionData> f) : data_(std::move(f)) {}

    static Value MakeArray(Array items = {}) {
        return Value(std::make_shared<Array>(std::move(items)));
    }
    static Value MakeObject(Object fields = {}) {
        return Value(std::make_shared<Object>(std::move(fields)));
    }
    static Value MakeFunction(std::vector<std::string> params, const Node* body) {
        auto f = std::make_shared<FunctionData>();
        f->params = std::move(params);
        f->body = body;
        return Value(std::move(f));
    }

    // ---- Kind ----
    ValueKind Kind() const {
        return static_cast<ValueKind>(data_.index());
    }
    bool IsNull()     const { return Kind() == ValueKind::Null; }
    bool IsNumber()   const { return Kind() == ValueKind::Number; }
    bool IsBool()     const { return Kind() == ValueKind::Bool; }
    bool IsString()   const { return Kind() == ValueKind::String; }
    bool IsArray()    const { return Kind() == ValueKind::Array; }
    bool IsObject()   const { return Kind() == ValueKind::Object; }
    bool IsFunction() const { return Kind() == ValueKind::Function; }

    const FunctionData* AsFunction() const {
        static const FunctionData empty;
        return IsFunction() ? std::get<std::shared_ptr<FunctionData>>(data_).get() : &empty;
    }

    // ---- Typed access (no coercion; throws nothing — returns default if wrong kind) ----
    double           Number() const { return IsNumber() ? std::get<double>(data_) : 0.0; }
    bool             Bool()   const { return IsBool()   ? std::get<bool>(data_)   : false; }
    const std::string& String() const {
        static const std::string empty;
        return IsString() ? std::get<std::string>(data_) : empty;
    }
    const Array&  AsArray() const {
        static const Array emptyArr;
        return IsArray() ? *std::get<std::shared_ptr<Array>>(data_) : emptyArr;
    }
    const Object& AsObject() const {
        static const Object emptyObj;
        return IsObject() ? *std::get<std::shared_ptr<Object>>(data_) : emptyObj;
    }
    Array& AsMutableArray() {
        return *std::get<std::shared_ptr<Array>>(data_);
    }
    Object& AsMutableObject() {
        return *std::get<std::shared_ptr<Object>>(data_);
    }

    // ---- Coercion (JS-ish) ----
    // "truthy": non-empty string, non-zero number, true, non-null
    bool ToBool() const {
        switch (Kind()) {
            case ValueKind::Null:   return false;
            case ValueKind::Bool:   return std::get<bool>(data_);
            case ValueKind::Number: {
                double n = std::get<double>(data_);
                return n != 0.0 && !std::isnan(n);
            }
            case ValueKind::String: return !std::get<std::string>(data_).empty();
            case ValueKind::Array:    return true;
            case ValueKind::Object:   return true;
            case ValueKind::Function: return true;
        }
        return false;
    }

    double ToNumber() const {
        switch (Kind()) {
            case ValueKind::Null:   return 0.0;
            case ValueKind::Bool:   return std::get<bool>(data_) ? 1.0 : 0.0;
            case ValueKind::Number: return std::get<double>(data_);
            case ValueKind::String: {
                const auto& s = std::get<std::string>(data_);
                if (s.empty()) return 0.0;
                try { return std::stod(s); } catch (...) { return std::nan(""); }
            }
            default: return std::nan("");
        }
    }

    std::string ToString() const {
        switch (Kind()) {
            case ValueKind::Null:   return "null";
            case ValueKind::Bool:   return std::get<bool>(data_) ? "true" : "false";
            case ValueKind::Number: {
                double n = std::get<double>(data_);
                if (std::isnan(n)) return "NaN";
                if (std::isinf(n)) return n < 0 ? "-Infinity" : "Infinity";
                // Format: integer if integral, else trim trailing zeros
                if (std::floor(n) == n && std::abs(n) < 1e16) {
                    return std::to_string(static_cast<long long>(n));
                }
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.15g", n);
                return buf;
            }
            case ValueKind::String:   return std::get<std::string>(data_);
            case ValueKind::Array:    return "[Array]";
            case ValueKind::Object:   return "[Object]";
            case ValueKind::Function: return "[Function]";
        }
        return "";
    }

    // ---- Equality ----
    friend bool operator==(const Value& a, const Value& b) {
        if (a.Kind() != b.Kind()) {
            // Numeric/bool cross-compare: JS-like loose eq only for Number/Bool
            if ((a.IsNumber() && b.IsBool()) || (a.IsBool() && b.IsNumber())) {
                return a.ToNumber() == b.ToNumber();
            }
            return false;
        }
        switch (a.Kind()) {
            case ValueKind::Null:   return true;
            case ValueKind::Bool:   return a.Bool() == b.Bool();
            case ValueKind::Number: return a.Number() == b.Number();
            case ValueKind::String: return a.String() == b.String();
            case ValueKind::Array:
                return std::get<std::shared_ptr<Array>>(a.data_)
                    == std::get<std::shared_ptr<Array>>(b.data_);
            case ValueKind::Object:
                return std::get<std::shared_ptr<Object>>(a.data_)
                    == std::get<std::shared_ptr<Object>>(b.data_);
            case ValueKind::Function:
                return std::get<std::shared_ptr<FunctionData>>(a.data_)
                    == std::get<std::shared_ptr<FunctionData>>(b.data_);
        }
        return false;
    }
    friend bool operator!=(const Value& a, const Value& b) { return !(a == b); }

private:
    std::variant<
        std::monostate,                     // 0: Null
        double,                             // 1: Number
        bool,                               // 2: Bool
        std::string,                        // 3: String
        std::shared_ptr<Array>,             // 4: Array
        std::shared_ptr<Object>,            // 5: Object
        std::shared_ptr<FunctionData>       // 6: Function
    > data_;
};

}  // namespace ui::expr

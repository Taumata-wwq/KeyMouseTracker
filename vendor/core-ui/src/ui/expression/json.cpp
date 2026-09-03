#include "json.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace ui::expr {
namespace {

// ============================================================================
// Parser
// ============================================================================

struct Parser {
    const char* p;
    const char* end;
    std::string err;

    bool error(const std::string& m) {
        if (err.empty()) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), " at offset %zu", (size_t)(p - start));
            err = m + buf;
        }
        return false;
    }

    const char* start;

    void skipWs() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++p;
            else break;
        }
    }

    bool parseValue(Value& out) {
        skipWs();
        if (p >= end) return error("unexpected end of input");
        char c = *p;
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') return parseString(out);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n') return parseNull(out);
        return error(std::string("unexpected character '") + c + "'");
    }

    bool parseObject(Value& out) {
        ++p;  // skip '{'
        Object obj;
        skipWs();
        if (p < end && *p == '}') { ++p; out = Value::MakeObject(std::move(obj)); return true; }
        while (true) {
            skipWs();
            if (p >= end || *p != '"') return error("object key must be a string");
            Value keyVal;
            if (!parseString(keyVal)) return false;
            std::string key = keyVal.String();
            skipWs();
            if (p >= end || *p != ':') return error("expected ':' after key");
            ++p;
            Value v;
            if (!parseValue(v)) return false;
            obj[std::move(key)] = std::move(v);
            skipWs();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; out = Value::MakeObject(std::move(obj)); return true; }
            return error("expected ',' or '}' in object");
        }
    }

    bool parseArray(Value& out) {
        ++p;  // skip '['
        Array arr;
        skipWs();
        if (p < end && *p == ']') { ++p; out = Value::MakeArray(std::move(arr)); return true; }
        while (true) {
            Value v;
            if (!parseValue(v)) return false;
            arr.push_back(std::move(v));
            skipWs();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; out = Value::MakeArray(std::move(arr)); return true; }
            return error("expected ',' or ']' in array");
        }
    }

    /* 把一个 BMP code point 编码进 UTF-8. */
    static void appendUtf8(std::string& s, uint32_t cp) {
        if (cp < 0x80) {
            s += (char)cp;
        } else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }

    static int hexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    }

    bool parseString(Value& out) {
        ++p;  // skip opening "
        std::string s;
        while (p < end) {
            unsigned char c = (unsigned char)*p++;
            if (c == '"') { out = Value(std::move(s)); return true; }
            if (c == '\\') {
                if (p >= end) return error("unterminated escape");
                char e = *p++;
                switch (e) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'u': {
                        if (p + 4 > end) return error("\\u: short hex");
                        int hi = (hexDigit(p[0]) << 12) | (hexDigit(p[1]) << 8) |
                                 (hexDigit(p[2]) << 4)  |  hexDigit(p[3]);
                        if (hi < 0) return error("\\u: bad hex");
                        p += 4;
                        uint32_t cp = (uint32_t)hi;
                        /* surrogate pair? */
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (p + 6 > end || p[0] != '\\' || p[1] != 'u') {
                                appendUtf8(s, 0xFFFD); break;
                            }
                            int lo = (hexDigit(p[2]) << 12) | (hexDigit(p[3]) << 8) |
                                     (hexDigit(p[4]) << 4)  |  hexDigit(p[5]);
                            if (lo < 0xDC00 || lo > 0xDFFF) {
                                appendUtf8(s, 0xFFFD); break;
                            }
                            p += 6;
                            cp = 0x10000 + (((cp - 0xD800) << 10) | ((uint32_t)lo - 0xDC00));
                        }
                        appendUtf8(s, cp);
                        break;
                    }
                    default: return error(std::string("bad escape '\\") + e + "'");
                }
            } else if (c < 0x20) {
                return error("unescaped control char in string");
            } else {
                s += (char)c;
            }
        }
        return error("unterminated string");
    }

    bool parseNumber(Value& out) {
        const char* s = p;
        if (*p == '-') ++p;
        while (p < end && *p >= '0' && *p <= '9') ++p;
        if (p < end && *p == '.') { ++p; while (p < end && *p >= '0' && *p <= '9') ++p; }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
        std::string num(s, p);
        try {
            out = Value(std::stod(num));
            return true;
        } catch (...) {
            return error("malformed number");
        }
    }

    bool parseBool(Value& out) {
        if (end - p >= 4 && std::string(p, p + 4) == "true")  { p += 4; out = Value(true);  return true; }
        if (end - p >= 5 && std::string(p, p + 5) == "false") { p += 5; out = Value(false); return true; }
        return error("expected true/false");
    }

    bool parseNull(Value& out) {
        if (end - p >= 4 && std::string(p, p + 4) == "null") { p += 4; out = Value(nullptr); return true; }
        return error("expected null");
    }
};

// ============================================================================
// Emitter
// ============================================================================

void emitString(std::ostringstream& o, const std::string& s) {
    o << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o << buf;
                } else {
                    o << (char)c;   // pass UTF-8 multibyte through as-is
                }
        }
    }
    o << '"';
}

void emitValue(std::ostringstream& o, const Value& v) {
    switch (v.Kind()) {
        case ValueKind::Null: o << "null"; break;
        case ValueKind::Bool: o << (v.Bool() ? "true" : "false"); break;
        case ValueKind::Number: {
            double n = v.Number();
            if (std::isnan(n) || std::isinf(n)) { o << "null"; break; }
            /* 整数形态打 int, 否则 g 格式 (跟 JS JSON.stringify 行为接近) */
            if (n == std::floor(n) && std::abs(n) < 1e16) {
                char buf[32]; std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
                o << buf;
            } else {
                char buf[32]; std::snprintf(buf, sizeof(buf), "%.17g", n);
                o << buf;
            }
            break;
        }
        case ValueKind::String:
            emitString(o, v.String()); break;
        case ValueKind::Array: {
            o << '[';
            const auto& arr = v.AsArray();
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i) o << ',';
                emitValue(o, arr[i]);
            }
            o << ']';
            break;
        }
        case ValueKind::Object: {
            o << '{';
            const auto& obj = v.AsObject();
            bool first = true;
            for (const auto& kv : obj) {
                if (!first) o << ',';
                first = false;
                emitString(o, kv.first);
                o << ':';
                emitValue(o, kv.second);
            }
            o << '}';
            break;
        }
        case ValueKind::Function:
            o << "null"; break;
    }
}

}  // namespace

bool ParseJson(const std::string& input, Value& out, std::string& errMsg) {
    Parser ps;
    ps.start = input.data();
    ps.p = input.data();
    ps.end = input.data() + input.size();
    Value tmp;
    if (!ps.parseValue(tmp)) {
        errMsg = std::move(ps.err);
        return false;
    }
    ps.skipWs();
    if (ps.p != ps.end) {
        errMsg = "trailing garbage";
        return false;
    }
    out = std::move(tmp);
    return true;
}

std::string EmitJson(const Value& v) {
    std::ostringstream o;
    emitValue(o, v);
    return o.str();
}

}  // namespace ui::expr

#include "debug_trace.h"

#include <atomic>

namespace {

std::atomic<UiTraceSink> g_trace_sink{nullptr};
std::atomic<void*>       g_trace_userdata{nullptr};

}  // namespace

extern "C" UI_API void ui_trace_set_sink(UiTraceSink sink, void* userdata) {
    g_trace_userdata.store(userdata, std::memory_order_release);
    g_trace_sink.store(sink, std::memory_order_release);
}

namespace ui {

bool IsTraceEnabled() {
    return g_trace_sink.load(std::memory_order_acquire) != nullptr;
}

void EmitTrace(const char* source,
               const char* category,
               const char* name,
               const UiTraceField* fields,
               uint32_t field_count) {
    UiTraceSink sink = g_trace_sink.load(std::memory_order_acquire);
    if (!sink) return;
    UiTraceEvent ev{};
    ev.source = source ? source : "";
    ev.category = category ? category : "";
    ev.name = name ? name : "";
    ev.field_count = field_count;
    ev.fields = fields;
    sink(&ev, g_trace_userdata.load(std::memory_order_acquire));
}

}  // namespace ui

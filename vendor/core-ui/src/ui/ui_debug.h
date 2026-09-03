/*
 * ui_debug.h — Widget tree debug inspector
 */
#pragma once

#include <string>

namespace ui {

class Widget;
class Renderer;

// Dump the widget tree rooted at `root` as a JSON string.
// If renderer is provided, text measurement (textWidth, lines) is included.
std::string DebugDumpTree(Widget* root, Renderer* renderer = nullptr, int depth = 0);

// 任意"文本控件"的文本值 (Label/Button/Check/Radio/Toggle/Combo/Input/TextArea/
// Nav/Overlay/TitleBar)。无文本语义的控件返回空串, has 非空时写入是否有文本。
std::wstring WidgetTextValue(Widget* w, bool* has = nullptr);

// 精简基础属性 json {"id","type","text"} — 比 DebugDumpTree 轻 (无 rect/metrics/children)。
std::string WidgetBasicJson(Widget* w);

} // namespace ui

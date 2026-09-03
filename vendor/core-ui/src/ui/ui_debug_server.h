#pragma once
//
// Library-level named-pipe debug server.
//
// Lifts the dispatchCommand + pipe thread that used to live in demo/app.cpp
// into core-ui itself, so any application (HTML demo, ChromePlusGUI, user
// projects) can opt into the standard debug protocol with one call:
//
//     ui_debug_server_start(win, NULL);   // listens on \\.\pipe\ui_core_debug
//
// All built-in commands are documented in docs/debug-simulation.md.
// Apps may register a custom handler to add private commands or override
// builtins (see ui_debug_server_set_handler).
//
// Thread model: a worker thread accepts pipe connections; each command is
// marshalled back to the UI thread via ui_window_invoke_sync before any
// widget state is touched.
//

#include "../../include/ui_core.h"

#include <string>

namespace ui::debug {

// Built-in dispatcher. cmd = first whitespace-delimited token of the line,
// rest = everything after the first separator (may be empty). Returns a JSON
// string (no trailing newline). Empty return means "command not handled" —
// caller should fall through to user handler / unknown-command response.
std::string BuiltinDispatch(UiWindow win, const std::string& cmd,
                            const std::string& rest);

}  // namespace ui::debug

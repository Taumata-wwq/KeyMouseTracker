// 全局低层键盘/鼠标钩子
#include "hooks.h"
#include "data.h"

static HHOOK g_kbd = nullptr, g_mouse = nullptr;
static bool g_keyDown[256] = {false};

static uint8_t normalizeModifierVK(DWORD vkCode, DWORD flags, DWORD scanCode) {
    switch (vkCode) {
        case VK_SHIFT:
            return (scanCode == 0x36) ? (uint8_t)VK_RSHIFT : (uint8_t)VK_LSHIFT;
        case VK_CONTROL:
            return (flags & LLKHF_EXTENDED) ? (uint8_t)VK_RCONTROL : (uint8_t)VK_LCONTROL;
        case VK_MENU:
            return (flags & LLKHF_EXTENDED) ? (uint8_t)VK_RMENU : (uint8_t)VK_LMENU;
        default:
            return (uint8_t)vkCode;
    }
}

static LRESULT CALLBACK kbdProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lParam;
        DWORD raw = k->vkCode;
        uint8_t vk = (raw < 256) ? normalizeModifierVK(raw, k->flags, k->scanCode) : 0;
        if (vk == 0) return CallNextHookEx(g_kbd, nCode, wParam, lParam);

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (!g_keyDown[vk]) {
                g_keyDown[vk] = true;
                recordKey(vk);
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            g_keyDown[vk] = false;
        }
    }
    return CallNextHookEx(g_kbd, nCode, wParam, lParam);
}

static LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* m = (MSLLHOOKSTRUCT*)lParam;
        uint8_t btn = 0;
        if (wParam == WM_LBUTTONDOWN) btn = 1;
        else if (wParam == WM_RBUTTONDOWN) btn = 2;
        else if (wParam == WM_MBUTTONDOWN) btn = 3;
        if (btn) recordClick(btn, m->pt.x, m->pt.y);
    }
    return CallNextHookEx(g_mouse, nCode, wParam, lParam);
}

bool InstallHooks() {
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    g_kbd = SetWindowsHookExW(WH_KEYBOARD_LL, kbdProc, hinst, 0);
    g_mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, hinst, 0);
    return g_kbd && g_mouse;
}
void UninstallHooks() {
    if (g_kbd) { UnhookWindowsHookEx(g_kbd); g_kbd = nullptr; }
    if (g_mouse) { UnhookWindowsHookEx(g_mouse); g_mouse = nullptr; }
}
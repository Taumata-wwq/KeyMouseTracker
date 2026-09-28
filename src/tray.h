#pragma once
#include <ui_core.h>
#include <windows.h>

// 系统托盘：图标 / 提示 / 菜单 / 命令分派 / 还原主窗口。
// 从 main.cpp 抽离，内部自持窗口句柄与托盘状态（NOTIFYICONDATAW / HMENU）。

// 托盘菜单命令 id（SubclassProc 通过 kTrayMsg 分派）
enum { IDM_SHOW = 1000, IDM_PAUSE = 1001, IDM_AUTOSTART = 1002, IDM_EXIT = 1003 };
constexpr UINT kTrayMsg = WM_APP + 1;   // 托盘回调消息（由 SubclassProc 处理）

void TrayInit(UiWindow win, HWND hwnd); // 记录主窗口句柄，须在 ui_window_hwnd 之后调用
UINT TrayShowMenu();                    // 按当前语言重建并弹出菜单，返回所选命令 id
void TrayAddIcon();                     // 添加托盘图标（可重复调用：重试 / 重建场景）
void TrayUpdate(bool paused);           // 刷新托盘提示文案
void TrayRefreshCheck();                // 同步「暂停 / 开机自启」勾选态
void TrayShowWindow();                  // 从托盘还原主窗口
void TrayHandleCommand(UINT cmd);       // 处理托盘菜单命令
void TrayCleanup();                     // 删除图标并销毁菜单（退出时调用）

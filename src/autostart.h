#pragma once
#include <windows.h>
#include <string>

// 开机自启动（计划任务：登录触发器 + 最高权限，兼容管理员权限程序）
bool SetAutoStart(bool enable);
bool IsAutoStart();
std::wstring GetExePath();
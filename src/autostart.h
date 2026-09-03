#pragma once
#include <windows.h>
#include <string>

// 开机自启动（HKCU Run 项）
bool SetAutoStart(bool enable);
bool IsAutoStart();
std::wstring GetExePath();
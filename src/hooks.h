#pragma once
#include <windows.h>

// 安装/卸载全局低层钩子（必须在安装线程的消息循环中运行）
bool InstallHooks();
void UninstallHooks();
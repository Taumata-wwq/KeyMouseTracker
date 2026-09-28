#pragma once
#include "data.h"

// 界面语言辅助：app().lang == 0 返回中文，1 返回 English。
// 供 UI 文案（main.cpp / tray.cpp 等模块）共享，避免各模块各自维护语言判断。
inline const wchar_t* tr(const wchar_t* zh, const wchar_t* en) {
    return app().lang == 1 ? en : zh;
}

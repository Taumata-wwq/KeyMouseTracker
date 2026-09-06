#pragma once
#include <string>

// 按日期索引区间 [startIdx, endIdx] 导出（含边界；全量传 0..65535）
bool ExportCSV(const std::wstring& path, int startIdx, int endIdx);
bool ExportJSON(const std::wstring& path, int startIdx, int endIdx);
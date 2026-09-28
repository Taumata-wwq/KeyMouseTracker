#pragma once
#include <string>

// 统计 JSON 构建（纯函数：只读 app() 数据，产出推送到 .uix 的 JSON 字符串）。
// 无 UI / 筛选 / 明细全局依赖；24h 排行的 EMA 平滑状态由本模块内部维护。
std::string BuildTodayJson();        // 今日概况 {keys,clicks,motion,distCm,activeSec}
std::string BuildTotalJson();        // 累计概况 {keys,clicks,days,activeSec,distCm}
std::string BuildStorageJson();      // 存储概况 {bytes,days,first,last}
std::string BuildExcludeListJson();  // 前台应用排除列表（v-for 数据源）
std::string BuildTopAppsJson();      // 今日活跃应用 Top6
std::string BuildApps24hJson();      // 24h 应用排行（评分 + EMA 平滑）

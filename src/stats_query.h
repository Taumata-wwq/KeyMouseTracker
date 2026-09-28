#pragma once
#include <map>
#include <string>
#include <cstdint>
#include <windows.h>
#include "data.h"

// —— 应用筛选 + 明细范围 查询上下文（自 main.cpp 抽离）——
// 状态定义于 stats_query.cpp；main.cpp 的绘图/命令轮询直接访问这些 extern 变量，
// 聚合与缓存重建逻辑由本模块函数封装。

// 筛选状态
extern std::string g_filterApp;        // 当前筛选应用（空串=全部）
extern bool        g_filterCacheDirty; // 聚合缓存失效标志
extern DWORD       g_filterCacheTick;  // 上次重建时刻（数据增量节流）

// 明细范围状态
extern bool        g_detailAllTime;    // true=全部时间；false=按 [startMin, endMin]
extern bool        g_detailDirty;      // 范围/模式/数据更新后需重建 detailS
extern std::string g_detailJson;       // 聚合结果缓存（供 pushKeyIfChanged diff）
extern uint32_t    g_detailCacheTick;  // 周期性刷新节流（每 5s 重算一次）

// 筛选应用全历史聚合（惰性缓存；exe 参数为兼容旧调用保留）
const std::map<uint8_t, uint32_t>& keysForApp(const std::string& exe);
const std::map<uint32_t, uint32_t>& heatForApp(const std::string& exe);

// 应用在 [ms, me) 分钟区间内的活跃分钟数（纯函数）
int appActiveMinutes(const DayData& d, const std::string& exe, int ms, int me);

// 解析并应用明细范围；全空 = 全部时间
void applyDetailRange(const std::string& s1, const std::string& s2);

// 构建明细聚合 JSON（按键排行 + 鼠标点击/里程/活跃汇总）
std::string buildDetailJson();

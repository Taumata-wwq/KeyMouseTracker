#pragma once
// 应用版本与数据格式版本单一来源。
// - KMT_APP_VERSION：About 页 / API 返回 / README 使用的产品版本号，随发布手工递增。
// - KMT_DATA_VERSION：data.bin 持久化格式版本（与 data.cpp 的 saveData/loadData 对应），
//   仅在数据布局变更时递增。
#define KMT_APP_NAME    L"KeyMouseTracker"
#define KMT_APP_TITLE   L"键鼠使用记录"
#define KMT_APP_VERSION "0.3.2"
#define KMT_DATA_VERSION 12

#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <windows.h>

extern const wchar_t* kAppName;

constexpr int kHeatW = 48;
constexpr int kHeatH = 27;

struct DayData {
    uint16_t day = 0;
    uint64_t keys = 0;
    uint64_t clicks = 0;
    uint32_t mLeft = 0, mMid = 0, mRight = 0;
    uint32_t activeSec = 0;
    uint64_t motion = 0;
    uint64_t distPx = 0;   // 鼠标位移（像素累计）
    // 活跃/空闲细分 + 长会话
    uint32_t idleSec = 0;         // 今日空闲总秒（连续无活动 ≥ 空闲阈值 5 分钟才计入）
    uint32_t maxSessionSec = 0;   // 今日最长连续活跃段（秒）
    uint16_t sessionCount = 0;    // 今日活跃段次数
    // 分钟级操作强度（APM）：分钟序号(0..1439) → 该分钟键+点击次数（稀疏，仅存非零）
    std::map<uint16_t, uint16_t> minuteActivity;
    std::map<uint16_t, uint16_t> keyMinuteActivity;   // 分钟级按键
    std::map<uint16_t, uint16_t> clickMinuteActivity; // 分钟级点击
    // 每小时每键计数：复合键 = hour*256 + vk → 次数（CSV 键×时间矩阵数据源）
    std::map<uint32_t, uint32_t> keyHourly;
    std::map<uint8_t, uint32_t> keyCounts;
    std::map<uint32_t, uint32_t> mouseHeat;
    // 前台活跃应用统计（仅 optAppTrack 开启时记录）exe 名 → 按键+点击次数
    std::map<std::string, uint64_t> appCounts;
    uint64_t hourlyKeys[24] = {0};
    uint64_t hourlyClicks[24] = {0};
};

struct AppData {
    std::map<uint16_t, DayData> days;
    std::set<uint8_t> hiddenKeys;
    uint16_t cur = 0;
    bool paused = false;
    bool darkTheme = false;
    uint8_t kbLayout = 0;
    bool optAppTrack = false;   // 前台活动应用统计（隐私默认关）
    uint8_t idleMin = 5;        // 空闲阈值（分钟，可配置）
    std::set<std::string> excludeApps;  // 前台应用排除列表（exe 名）
    uint32_t lastActivity = 0;
    bool dirty = false;
    bool needsRefresh = false;
};

AppData& app();

const std::map<uint8_t, uint32_t>& cumulativeKeys();
const std::map<uint32_t, uint32_t>& cumulativeHeat();

int  dayIndexFromYMD(int y, int m, int d);
int  daysInMonth(int y, int m);
std::string dayIndexToStr(int idx);

std::wstring dataFilePath();

bool loadData(const std::wstring& path);
bool saveData(const std::wstring& path);
void ensureCurDay();
void clearAllData();

void recordKey(uint8_t vk);
void recordClick(uint8_t btn, LONG x, LONG y);
void recordMove();
void recordMoveDist(uint64_t px);

// 像素 → 厘米（按系统逻辑 DPI 折算，1px = 25.4mm / dpi）。供 UI 与导出共用
uint64_t distToCm(uint64_t px);

// 当前前台应用（exe 名，UTF-8）：由 main 定时轮询刷新，供按键/点击归因
const std::string& currentForeApp();
void setCurrentForeApp(const std::string& name);

// 数据管理：按日期索引区间删除（含该区间全部日数据），之后由调用方 ensureCurDay 重建
void eraseRange(int startIdx, int endIdx);
// 存储概况：数据文件字节数、记录天数、最早/最晚日期索引（无数据时 first/last 为 0）
struct StorageInfo { uint64_t bytes = 0; int days = 0; int first = 0; int last = 0; };
StorageInfo storageInfo();

int  heatIndexFromScreen(LONG x, LONG y);

// 按键虚拟键码 → 可读名称（写入调用方 buf，返回同指针；UTF-8）。UI 与导出共用
const char* vkLabel(uint8_t vk, char buf[32]);
// JSON 字符串转义（UTF-8 字节流）：引号/反斜杠/控制字符（<0x20 → \uXXXX）
std::string jsonEscape(const char* s);
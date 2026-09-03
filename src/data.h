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
    std::map<uint8_t, uint32_t> keyCounts;
    std::map<uint32_t, uint32_t> mouseHeat;
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

int  heatIndexFromScreen(LONG x, LONG y);
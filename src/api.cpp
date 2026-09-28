// 本机命名管道 JSON API（只读查询）。
// 后台线程只做管道收发；取数在主线程 ApiServe() 内完成（app() 仅主线程读写，避免数据竞争）。
// 协议：客户端连接 → 写一行命令 → 服务端返回一行 JSON → 断开。命令见 docs/API.md。
#include "api.h"
#include "data.h"
#include "version.h"
#include <windows.h>
#include <process.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <set>
#include <algorithm>

static const char* kPipeName = "\\\\.\\pipe\\KeyMouseTrackerApi";

// —— 共享请求/响应槽（后台线程 ↔ 主线程）——
static CRITICAL_SECTION g_cs;
static HANDLE g_evtRequest = nullptr;   // 后台线程置位：请求已就绪（自动复位）
static HANDLE g_evtResponse = nullptr;  // 主线程置位：响应已就绪（自动复位）
static std::string g_req;               // 请求命令（已去尾部换行）
static std::string g_resp;              // 响应 JSON（单行）
static volatile LONG g_stop = 0;
static HANDLE g_thread = nullptr;

// ===== 命令解析与 JSON 构建（仅在主线程 ApiServe 内调用）=====

// 数字转字符串（避免 locale 千分位影响 JSON）
static std::string u64s(uint64_t v) {
    char b[24];
    snprintf(b, sizeof(b), "%llu", (unsigned long long)v);
    return b;
}

static bool parseDate(const char* s, int& y, int& m, int& d) {
    if (!s) return false;
    int a = 0, b = 0, c = 0;
    if (sscanf(s, "%d-%d-%d", &a, &b, &c) != 3) return false;
    if (a < 1970 || a > 2100 || b < 1 || b > 12 || c < 1 || c > 31) return false;
    y = a; m = b; d = c;
    return true;
}

static std::string langTag() { return app().lang == 0 ? "zh" : "en"; }

static std::string buildPingJson() {
    return std::string("{\"ok\":true,\"app\":\"KeyMouseTracker\",\"version\":\"")
        + KMT_APP_VERSION + "\",\"lang\":\"" + langTag() + "\"}";
}

static std::string buildStateJson() {
    std::string s = "{\"paused\":";
    s += app().paused ? "true" : "false";
    s += ",\"dark\":";
    s += app().darkTheme ? "true" : "false";
    s += ",\"lang\":\"" + langTag() + "\"";
    s += ",\"foreApp\":\"" + jsonEscape(currentForeApp().c_str()) + "\"";
    s += ",\"recording\":";
    s += (app().optAppTrack && !app().paused) ? "true" : "false";
    s += "}";
    return s;
}

static std::string buildSummaryJson() {
    uint64_t keys = 0, clicks = 0, motion = 0, distPx = 0, activeSec = 0;
    int days = 0;
    std::set<std::string> apps;
    for (auto& kv : app().days) {
        const DayData& d = kv.second;
        ++days;
        keys += d.keys; clicks += d.clicks; motion += d.motion;
        distPx += d.distPx; activeSec += d.activeSec;
        for (auto& ap : d.appMin) apps.insert(ap.first);
    }
    return std::string("{\"days\":") + u64s(days)
        + ",\"keys\":" + u64s(keys)
        + ",\"clicks\":" + u64s(clicks)
        + ",\"motion\":" + u64s(motion)
        + ",\"distCm\":" + u64s(distToCm(distPx))
        + ",\"activeSec\":" + u64s(activeSec)
        + ",\"apps\":" + u64s(apps.size()) + "}";
}

static std::string buildTodayJson() {
    ensureCurDay();
    const DayData& d = app().days[app().cur];
    return std::string("{\"date\":\"") + dayIndexToStr((int)d.day) + "\""
        + ",\"keys\":" + u64s(d.keys)
        + ",\"clicks\":" + u64s(d.clicks)
        + ",\"left\":" + u64s(d.mLeft)
        + ",\"mid\":" + u64s(d.mMid)
        + ",\"right\":" + u64s(d.mRight)
        + ",\"motion\":" + u64s(d.motion)
        + ",\"distCm\":" + u64s(distToCm(d.distPx))
        + ",\"activeSec\":" + u64s(d.activeSec)
        + ",\"idleSec\":" + u64s(d.idleSec)
        + ",\"maxSessionSec\":" + u64s(d.maxSessionSec)
        + ",\"sessionCount\":" + u64s(d.sessionCount)
        + ",\"apps\":" + u64s(d.appMin.size()) + "}";
}

static std::string buildAppsJson() {
    ensureCurDay();
    const DayData& d = app().days[app().cur];
    struct Item { std::string name; uint64_t keys, clicks, activeMin; };
    std::vector<Item> items;
    items.reserve(d.appMin.size());
    for (auto& ap : d.appMin) {
        Item it;
        it.name = ap.first;
        it.keys = appKeys(d, ap.first, -1, -1);
        it.clicks = appClicks(d, ap.first, -1, -1);
        it.activeMin = appActiveMin(d, ap.first, -1, -1);
        items.push_back(it);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return (a.keys + a.clicks) > (b.keys + b.clicks);
    });
    if (items.size() > 20) items.resize(20);
    std::string s = "{\"date\":\"" + dayIndexToStr((int)d.day) + "\",\"apps\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) s += ",";
        s += "{\"name\":\"" + jsonEscape(items[i].name.c_str()) + "\""
           + ",\"keys\":" + u64s(items[i].keys)
           + ",\"clicks\":" + u64s(items[i].clicks)
           + ",\"activeMin\":" + u64s(items[i].activeMin) + "}";
    }
    s += "]}";
    return s;
}

static std::string buildRangeJson(int startIdx, int endIdx) {
    uint64_t keys = 0, clicks = 0, motion = 0, distPx = 0, activeSec = 0;
    int days = 0;
    for (auto& kv : app().days) {
        int idx = (int)kv.first;
        if (idx < startIdx || idx > endIdx) continue;
        const DayData& d = kv.second;
        ++days;
        keys += d.keys; clicks += d.clicks; motion += d.motion;
        distPx += d.distPx; activeSec += d.activeSec;
    }
    return std::string("{\"start\":\"") + dayIndexToStr(startIdx)
        + "\",\"end\":\"" + dayIndexToStr(endIdx) + "\""
        + ",\"days\":" + u64s(days)
        + ",\"keys\":" + u64s(keys)
        + ",\"clicks\":" + u64s(clicks)
        + ",\"motion\":" + u64s(motion)
        + ",\"distCm\":" + u64s(distToCm(distPx))
        + ",\"activeSec\":" + u64s(activeSec) + "}";
}

// 主线程：解析命令并构建响应 JSON
static std::string buildResponse(const std::string& cmd) {
    // 大小写不敏感：取首 token
    std::string tok;
    size_t sp = cmd.find_first_of(" \t");
    tok = cmd.substr(0, sp);
    std::transform(tok.begin(), tok.end(), tok.begin(),
        [](unsigned char c) { return (char)tolower(c); });

    if (tok == "ping") return buildPingJson();
    if (tok == "state") return buildStateJson();
    if (tok == "summary") return buildSummaryJson();
    if (tok == "today") return buildTodayJson();
    if (tok == "apps") return buildAppsJson();
    if (tok == "range") {
        // 解析 "range YYYY-MM-DD [YYYY-MM-DD]"：缺省即全部历史
        int sy = 0, sm = 0, sd = 0, ey = 0, em = 0, ed = 0;
        std::string rest = (sp == std::string::npos) ? "" : cmd.substr(sp + 1);
        std::vector<std::string> parts;
        size_t i = 0;
        while (i <= rest.size()) {
            size_t j = rest.find_first_of(" \t", i);
            std::string p = rest.substr(i, j == std::string::npos ? std::string::npos : j - i);
            if (!p.empty()) parts.push_back(p);
            if (j == std::string::npos) break;
            i = j + 1;
        }
        bool hasStart = !parts.empty() && parseDate(parts[0].c_str(), sy, sm, sd);
        bool hasEnd = parts.size() >= 2 && parseDate(parts[1].c_str(), ey, em, ed);
        int startIdx = hasStart ? dayIndexFromYMD(sy, sm, sd) : -1;
        int endIdx = hasEnd ? dayIndexFromYMD(ey, em, ed) : -1;
        if (startIdx < 0) {   // 无有效起始：取最早一天
            startIdx = app().days.empty() ? 0 : app().days.begin()->first;
        }
        if (endIdx < 0) {     // 无有效结束：取最晚一天
            endIdx = app().days.empty() ? 0 : app().days.rbegin()->first;
        }
        if (startIdx > endIdx) std::swap(startIdx, endIdx);
        return buildRangeJson(startIdx, endIdx);
    }
    return std::string("{\"error\":\"unknown command: ") + jsonEscape(tok.c_str()) + "\"}";
}

// ===== 后台管道线程 =====

static unsigned __stdcall ApiThreadProc(void*) {
    while (!g_stop) {
        HANDLE pipe = CreateNamedPipeA(kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 65536, 65536, 1000, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) { Sleep(500); continue; }

        // 阻塞等待客户端连接；返回 0 且 ERROR_PIPE_CONNECTED 表示客户端已抢先连接
        if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            continue;
        }

        // 等待客户端发送数据（超时 5s）：避免「连接后不发送」的空连接/异常客户端
        // 永久阻塞 ReadFile，导致服务线程挂死、后续请求无法响应。
        bool hasData = false;
        for (int i = 0; i < 50 && !g_stop; ++i) {   // 50 × 100ms = 5s
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) break;  // 客户端已断开
            if (avail > 0) { hasData = true; break; }
            Sleep(100);
        }
        if (!hasData) { DisconnectNamedPipe(pipe); CloseHandle(pipe); continue; }

        char buf[4096];
        DWORD rd = 0;
        if (ReadFile(pipe, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
            buf[rd] = 0;
            std::string cmd(buf);
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();

            // 交给主线程取数（生产-消费）
            EnterCriticalSection(&g_cs);
            g_req = cmd;
            g_resp.clear();
            ResetEvent(g_evtResponse);
            LeaveCriticalSection(&g_cs);
            SetEvent(g_evtRequest);

            DWORD wr = WaitForSingleObject(g_evtResponse, 2000);
            EnterCriticalSection(&g_cs);
            std::string resp = g_resp;
            LeaveCriticalSection(&g_cs);
            if (wr != WAIT_OBJECT_0 || resp.empty()) resp = "{\"error\":\"timeout\"}";

            resp += '\n';
            DWORD w = 0;
            WriteFile(pipe, resp.data(), (DWORD)resp.size(), &w, nullptr);
            FlushFileBuffers(pipe);
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

bool ApiStart() {
    InitializeCriticalSection(&g_cs);
    g_evtRequest = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_evtResponse = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_evtRequest || !g_evtResponse) return false;
    g_stop = 0;
    g_thread = (HANDLE)_beginthreadex(nullptr, 0, ApiThreadProc, nullptr, 0, nullptr);
    return g_thread != nullptr;
}

void ApiServe() {
    if (WaitForSingleObject(g_evtRequest, 0) != WAIT_OBJECT_0) return;   // 自动复位：有请求即消费
    EnterCriticalSection(&g_cs);
    std::string cmd = g_req;
    LeaveCriticalSection(&g_cs);
    std::string resp = buildResponse(cmd);   // 主线程取数，保证线程安全
    EnterCriticalSection(&g_cs);
    g_resp = resp;
    LeaveCriticalSection(&g_cs);
    SetEvent(g_evtResponse);
}

void ApiStop() {
    InterlockedExchange(&g_stop, 1);
    // 连接一次以解除后台线程在 ConnectNamedPipe / ReadFile 上的阻塞
    HANDLE h = CreateFileA(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (g_thread) {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_evtRequest) { CloseHandle(g_evtRequest); g_evtRequest = nullptr; }
    if (g_evtResponse) { CloseHandle(g_evtResponse); g_evtResponse = nullptr; }
    DeleteCriticalSection(&g_cs);
}

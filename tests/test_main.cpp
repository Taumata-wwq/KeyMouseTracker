// 单元测试：覆盖纯逻辑（日期算法 / varint 编解码 / JSON-CSV 转义 / 迁移分摊 / 时间格式化）。
// 采用 unity build 直接包含实现文件，以便访问其中的 static 函数；仅链接纯逻辑，
// 不触碰 UI / 钩子 / 持久化 IO（这些函数被编译但不被调用）。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>

#include "../src/data.cpp"
#include "../src/export.cpp"
#include "../src/stats_json.cpp"
#include "../src/stats_query.cpp"
#include "../src/uiutil.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) \
    do { if (cond) { ++g_pass; } else { ++g_fail; printf("FAIL: %s (line %d)\n", #cond, __LINE__); } } while (0)

// 比较两侧值（支持 std::string / 数值；要求两侧类型可 ==）
#define CHECK_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); if (_a == _b) { ++g_pass; } \
         else { ++g_fail; printf("FAIL: %s == %s (line %d)\n", #a, #b, __LINE__); } } while (0)

int main() {
    // ---- 日期算法（days_from_civil 为基准）----
    CHECK_EQ(dayIndexFromYMD(2020, 1, 1), 0);          // kBase 起点
    CHECK_EQ(dayIndexFromYMD(2020, 1, 2), 1);
    CHECK_EQ(dayIndexFromYMD(2020, 2, 1), 31);
    CHECK_EQ(dayIndexToStr(0), std::string("2020-01-01"));
    CHECK_EQ(dayIndexToStr(dayIndexFromYMD(2026, 9, 15)), std::string("2026-09-15"));
    CHECK_EQ(dayIndexToStr(dayIndexFromYMD(2024, 2, 29)), std::string("2024-02-29"));  // 闰年
    CHECK_EQ(dayIndexToStr(dayIndexFromYMD(2100, 1, 1)), std::string("2100-01-01"));  // 世纪非闰
    CHECK_EQ(daysInMonth(2024, 2), 29);
    CHECK_EQ(daysInMonth(2023, 2), 28);
    CHECK_EQ(daysInMonth(2026, 9), 30);
    CHECK_EQ(daysInMonth(2026, 12), 31);
    CHECK_EQ(daysInMonth(2026, 13), 31);   // 越界月份钳制为 12 月
    // 往返一致性：任意日起始 + 跨度内每天
    for (int d = 0; d < 400; d += 37) {
        std::string s = dayIndexToStr(d);
        // 反解析（用 sscanf 拆分）验证
        int y, m, dd;
        if (sscanf(s.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) {
            CHECK_EQ(dayIndexFromYMD(y, m, dd), d);
        }
    }

    // ---- varint 编解码往返 ----
    {
        unsigned long long vals[] = {
            0, 1, 127, 128, 255, 256, 16383, 16384, 16385,
            0xFFFFFFFFull, 0x100000000ull, 0xFFFFFFFFFFFFull, 123456789012345ull
        };
        for (unsigned long long v : vals) {
            std::vector<unsigned char> buf;
            writeVarint(buf, v);
            const unsigned char* p = buf.data();
            CHECK_EQ(readVarint(p, buf.data() + buf.size()), v);
            CHECK_EQ((size_t)(p - buf.data()), buf.size());   // 恰好消费完
        }
    }

    // ---- jsonEscape ----
    CHECK_EQ(jsonEscape("abc"), std::string("abc"));
    CHECK_EQ(jsonEscape("a\"b"), std::string("a\\\"b"));
    CHECK_EQ(jsonEscape("a\\b"), std::string("a\\\\b"));
    CHECK_EQ(jsonEscape("a\nb"), std::string("a\\nb"));
    CHECK_EQ(jsonEscape("a\tb"), std::string("a\\tb"));
    CHECK_EQ(jsonEscape("a\x01" "b"), std::string("a\\u0001b"));   // 控制字符 → \uXXXX（相邻字面量避免 \x01b 被当 0x1B）

    // ---- csvEscape ----
    CHECK_EQ(csvEscape("plain"), std::string("plain"));
    CHECK_EQ(csvEscape("a,b"), std::string("\"a,b\""));
    CHECK_EQ(csvEscape("a\"b"), std::string("\"a\"\"b\""));
    CHECK_EQ(csvEscape("a\r\nb"), std::string("\"a\r\nb\""));

    // ---- minToHHMM ----
    CHECK_EQ(minToHHMM(0), std::string("00:00"));
    CHECK_EQ(minToHHMM(59), std::string("00:59"));
    CHECK_EQ(minToHHMM(60), std::string("01:00"));
    CHECK_EQ(minToHHMM(1439), std::string("23:59"));

    // ---- uiutil.h 纯工具（自 main.cpp 抽离）----
    {
        // jsonInt
        CHECK_EQ(jsonInt("{\"x\":42}", 0), 42);
        CHECK_EQ(jsonInt("abc", 7), 7);
        CHECK_EQ(jsonInt(nullptr, 5), 5);
        CHECK_EQ(jsonInt("-3", 0), -3);
        // jsonText
        CHECK_EQ(jsonText("\"hello\"", "f"), std::string("hello"));
        CHECK_EQ(jsonText("no quotes here", "f"), std::string("f"));
        CHECK_EQ(jsonText(nullptr, "f"), std::string("f"));
        // parseYMDLoose
        int y, m, d;
        CHECK(parseYMDLoose("2026-08-30", 8, y, m, d));
        CHECK_EQ(y, 2026); CHECK_EQ(m, 8); CHECK_EQ(d, 30);
        CHECK(parseYMDLoose("20260830", 8, y, m, d));
        CHECK_EQ(y, 2026); CHECK_EQ(m, 8); CHECK_EQ(d, 30);
        CHECK(parseYMDLoose("2026.08.30", 8, y, m, d));
        CHECK_EQ(d, 30);
        CHECK(parseYMDLoose("2026-08", 6, y, m, d));
        CHECK_EQ(y, 2026); CHECK_EQ(m, 8); CHECK_EQ(d, 1);
        CHECK(!parseYMDLoose("2026-02-30", 8, y, m, d));   // 2 月 30 日非法
        CHECK(!parseYMDLoose("abc", 8, y, m, d));
        // parseDetailHHMM
        int h, n;
        parseDetailHHMM("2026-09-16 14:30", h, n);
        CHECK_EQ(h, 14); CHECK_EQ(n, 30);
        parseDetailHHMM("garbage", h, n);
        CHECK_EQ(h, 0); CHECK_EQ(n, 0);
        // ymdFromDayIndex（与 dayIndexFromYMD 互逆）
        ymdFromDayIndex(dayIndexFromYMD(2026, 9, 16), y, m, d);
        CHECK_EQ(y, 2026); CHECK_EQ(m, 9); CHECK_EQ(d, 16);
        // parseSeriesFlags
        auto idx = parseSeriesFlags("[1,0,1,0]");
        CHECK_EQ((int)idx.size(), 2);
        CHECK_EQ(idx[0], 0); CHECK_EQ(idx[1], 2);
        auto idx2 = parseSeriesFlags("[0,0,0,0]");
        CHECK_EQ((int)idx2.size(), 0);
    }

    // ---- stats_json 模块（空数据冒烟测试）----
    {
        CHECK(!BuildTodayJson().empty());
        CHECK(BuildTotalJson().find("\"days\":") != std::string::npos);
        CHECK_EQ(BuildExcludeListJson(), std::string("[]"));   // 空数据无排除项
        CHECK_EQ(BuildTopAppsJson(), std::string("[]"));        // 空数据无应用
        CHECK_EQ(BuildApps24hJson(), std::string("[]"));
    }

    // ---- stats_query 模块（筛选/明细 冒烟测试）----
    {
        // 空数据：明细聚合返回空排行 JSON
        std::string dj = buildDetailJson();
        CHECK(dj.find("\"kbd\":[]") != std::string::npos);
        // applyDetailRange 空参 → 回落全部时间
        applyDetailRange("", "");
        CHECK_EQ(g_detailAllTime, true);
        // 应用活跃分钟：空数据无该应用 → 0
        DayData empty;
        CHECK_EQ(appActiveMinutes(empty, "foo.exe", 0, 1440), 0);
    }

    // ---- LZSS 无损压缩往返（data.cpp 内部实现）----
    {
        // 模拟数据：重复 exe 名 + 变长结构（模拟序列化后的 v12 数据流）
        std::vector<uint8_t> src;
        const char* names[] = { "chrome.exe", "firefox.exe", "msedge.exe", "explorer.exe", "KeyMouseTracker.exe" };
        for (int day = 0; day < 10; ++day) {
            for (int a = 0; a < 5; ++a) {
                size_t nl = strlen(names[a]);
                src.insert(src.end(), (const uint8_t*)names[a], (const uint8_t*)names[a] + nl);
                for (int m = 0; m < 20; ++m) { src.push_back((uint8_t)(m & 0xFF)); src.push_back((uint8_t)(day * 3 + a)); }
            }
        }
        // 尾部随机段（压缩应保留）
        for (int i = 0; i < 200; ++i) src.push_back((uint8_t)(i * 31 + 7));

        std::vector<uint8_t> compressed, decompressed;
        CompressLZSS(src.data(), src.size(), compressed);
        bool ok = DecompressLZSS(compressed.data(), compressed.size(), decompressed);
        CHECK(ok);
        CHECK_EQ(decompressed.size(), src.size());
        CHECK(decompressed == src);
        // 重复模式数据压缩应有效
        CHECK(compressed.size() < src.size());
        // 空数据不崩溃
        std::vector<uint8_t> e1, e2;
        CompressLZSS(nullptr, 0, e1);
        CHECK(DecompressLZSS(e1.data(), e1.size(), e2));
        CHECK_EQ(e2.size(), (size_t)0);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

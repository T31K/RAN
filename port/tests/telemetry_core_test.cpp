// port/platform/telemetry_core.h: report JSON, base64 and the game-error throttle.
#include "../platform/telemetry_core.h"
#include <cstdio>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

using namespace ran_telemetry;

int main()
{
    // JSON strings: quotes, backslashes, control characters; bytes >= 0x80 (CP949 game text,
    // not valid UTF-8) become \u00XX so the report is always valid JSON.
    CHECK(JsonString("a\"b\\c") == "\"a\\\"b\\\\c\"");
    CHECK(JsonString("l1\nl2\tx\r") == "\"l1\\nl2\\tx\\r\"");
    CHECK(JsonString(std::string("\x01", 1)) == "\"\\u0001\"");
    CHECK(JsonString("\xb0\xa1") == "\"\\u00b0\\u00a1\"");

    Report r;
    r.Set("type", "hotkey");
    r.Set("account", "mozart1234");
    r.Set("account", "T31K");            // later Set replaces
    r.SetNumber("ram_gb", 16);
    CHECK(r.Json() == "{\"type\":\"hotkey\",\"account\":\"T31K\",\"ram_gb\":16}");

    CHECK(Base64("") == "");
    CHECK(Base64("f") == "Zg==");
    CHECK(Base64("fo") == "Zm8=");
    CHECK(Base64("foo") == "Zm9v");
    CHECK(Base64(std::string("\x00\xff\x10", 3)) == "AP8Q");

    CHECK(IsErrorLine("ERROR : GLMapAxisInfo::LoadFile(), w_total.mmp"));
    CHECK(IsErrorLine("texture load FAIL x.dds"));
    CHECK(IsErrorLine("[JOINDBG] ChaJoinJob ReadUserInven ret=0 (DB_ERROR means bail)"));
    CHECK(!IsErrorLine("[INPUTDBG] DxInputDevice::OnActivate(1)"));
    CHECK(!IsErrorLine("-----------------  ERROR REPORT  -----------------"));   // the log banner

    CHECK(Normalize("[2026-10-05 01:47:34] ERROR : GUID 600 x12") == "[-- ::] ERROR : GUID  x");

    // Throttle: <= 1 report per 60 s, <= 10 per session, the same line (digits aside) once.
    ErrorThrottle t(60.0, 10);
    CHECK(t.Allow("ERROR a 1", 0.0));
    CHECK(!t.Allow("ERROR b", 30.0));          // too soon
    CHECK(t.Allow("ERROR b", 61.0));
    CHECK(!t.Allow("ERROR a 2", 200.0));       // same line as the first, digits aside
    double at = 300;
    int more = 0;
    for (int i = 0; i < 20; ++i, at += 61) more += t.Allow("ERROR line " + std::string(1, char('c' + i)), at);
    CHECK(more == 8);                          // 2 + 8 = the session cap of 10

    if (g_fail) return 1;
    std::printf("telemetry_core_test: ok\n");
    return 0;
}

// Error reports from the native client - see ran_telemetry.h.
//
// Reports are JSON files in ~/Library/Application Support/RanOdyssey Native/reports/queue, sent
// by one background thread (kept and retried when offline). A crash cannot build or send
// anything: the signal handler writes a context line prepared earlier plus the backtrace to
// reports/pending-crash.txt with signal-safe calls only, and the next launch turns that into a
// report with the macOS crash log (.ips) attached.
#include "ran_telemetry.h"
#include "telemetry_core.h"
#include "telemetry_mac.h"
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <execinfo.h>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

bool RanCaptureBackBuffer(IDirect3DDevice9* dev, std::vector<unsigned char>& bgra, unsigned& w, unsigned& h);   // snapshot.cpp
extern "C" FILE* ran_fopen(const char* path, const char* mode);   // c_bridge.cpp: resolves the game's Windows-style paths

namespace ran_telemetry {
namespace {

namespace mac = ran_telemetry_mac;
using Clock = std::chrono::steady_clock;

const char* const kDefaultUrl = "https://api.kaleidoscopical.com/ran/reports";
const size_t kQueueCap = 50u << 20;
const int kLogTailLines = 500, kStderrTailLines = 200;

struct State {
    bool enabled = false;
    std::string url, key, dir, queue, pendingCrash, stderrLog;
    Clock::time_point start = Clock::now();

    // fixed facts, gathered once
    std::string installId, appVersion, appBuild, macos, model, chip, gpu, gameDir;
    long long ramGb = 0;

    std::mutex mu;                       // guards everything below
    std::string account, logFile, pendingErrorLine;
    bool hotkeyPending = false;
    double lastHotkey = -1e9;
    ErrorThrottle throttle{60.0, 10};
    std::deque<std::string> notices;     // shown in chat on the game thread
    std::condition_variable wake;

    GameState (*stateHook)() = nullptr;
    void (*notifyHook)(const char*) = nullptr;
    double lastCrashHead = -1e9;
    double testCrashAt = -1;
};

State& S()
{
    static State* s = new State;   // never destroyed: the process ends with _exit
    return *s;
}

// Crash context, double-buffered so the signal handler never reads a half-written line.
char g_crashHead[2][16384];
std::atomic<int> g_crashHeadIdx{0};
std::atomic<size_t> g_crashHeadLen[2];
char g_pendingCrashPath[1024];

double Uptime() { return std::chrono::duration<double>(Clock::now() - S().start).count(); }

std::string ReadFile(const std::string& p)
{
    // The game's log path is Windows-style (backslashes, case-insensitive): open it the way the
    // game does.
    FILE* f = ran_fopen(p.c_str(), "rb");
    if (!f) return "";
    std::string out;
    char buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0; ) out.append(buf, n);
    std::fclose(f);
    return out;
}

std::string TailLines(const std::string& path, int lines)
{
    const std::string all = ReadFile(path);
    if (all.empty()) return "";
    size_t at = all.size();
    for (int n = 0; at > 0 && n <= lines; ) {
        at = all.rfind('\n', at - 1);
        if (at == std::string::npos) { at = 0; break; }
        ++n;
    }
    return all.substr(at ? at + 1 : 0);
}

std::string NewestFile(const std::string& dir, const std::string& prefix, const std::string& suffix, time_t notBefore = 0)
{
    std::string best;
    time_t bestTime = notBefore;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.compare(0, prefix.size(), prefix) != 0 || n.size() < suffix.size() ||
                n.compare(n.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            struct stat st;
            const std::string p = dir + "/" + n;
            if (::stat(p.c_str(), &st) == 0 && st.st_mtime >= bestTime) { bestTime = st.st_mtime; best = p; }
        }
        closedir(d);
    }
    return best;
}

std::string GameLogFile()
{
    State& s = S();
    {
        std::lock_guard<std::mutex> l(s.mu);
        if (!s.logFile.empty()) return s.logFile;
    }
    const char* home = std::getenv("HOME");
    return NewestFile(std::string(home ? home : "") + "/Library/Application Support/RanOdyssey/RanOnline/errlog", "log.", ".txt");
}

void MkdirP(const std::string& path)
{
    for (size_t at = 1; at != std::string::npos; ) {
        at = path.find('/', at + 1);
        ::mkdir(path.substr(0, at).c_str(), 0755);
    }
}

std::string UtcNow()
{
    char b[32];
    const time_t t = time(nullptr);
    strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    return b;
}

// Everything known about this player and Mac. Game thread only (SDL video, the state hook).
Report Context(const char* type)
{
    State& s = S();
    Report r;
    r.Set("type", type);
    {
        std::lock_guard<std::mutex> l(s.mu);
        r.Set("account", s.account);
    }
    if (s.stateHook) {
        const GameState g = s.stateHook();
        r.Set("character", g.character ? g.character : "");
        if (g.mapMain >= 0) r.Set("map", std::to_string(g.mapMain) + "/" + std::to_string(g.mapSub));
    }
    r.Set("app_version", s.appVersion);
    r.Set("app_build", s.appBuild);
    r.Set("macos", s.macos);
    r.Set("mac_model", s.model);
    r.Set("chip", s.chip);
    r.SetNumber("ram_gb", (double)s.ramGb);
    r.Set("gpu", s.gpu);
    std::string displays;
    int n = 0;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&n)) {
        for (int i = 0; i < n; ++i) {
            const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(ids[i]);
            const char* name = SDL_GetDisplayName(ids[i]);
            char b[256];
            std::snprintf(b, sizeof(b), "%s%s %dx%d@%.0fHz scale %.1f", i ? "; " : "", name ? name : "?",
                          m ? m->w : 0, m ? m->h : 0, m ? m->refresh_rate : 0.f, SDL_GetDisplayContentScale(ids[i]));
            displays += b;
        }
        SDL_free(ids);
    }
    r.Set("displays", displays);
    int nw = 0;
    if (SDL_Window** wins = SDL_GetWindows(&nw)) {
        if (nw > 0) {
            int w = 0, h = 0, pw = 0, ph = 0;
            SDL_GetWindowSize(wins[0], &w, &h);
            SDL_GetWindowSizeInPixels(wins[0], &pw, &ph);
            const char* dn = SDL_GetDisplayName(SDL_GetDisplayForWindow(wins[0]));
            char b[256];
            std::snprintf(b, sizeof(b), "%dx%d (%dx%d px) on %s%s", w, h, pw, ph, dn ? dn : "?",
                          (SDL_GetWindowFlags(wins[0]) & SDL_WINDOW_FULLSCREEN) ? ", fullscreen" : "");
            r.Set("window", b);
        }
        SDL_free(wins);
    }
    r.Set("game_dir", s.gameDir);
    r.Set("install_id", s.installId);
    r.SetNumber("uptime_s", (double)(long long)Uptime());
    r.Set("client_time", UtcNow());
    return r;
}

void RefreshCrashHead()
{
    Report r = Context("crash");
    r.Set("log_file", GameLogFile());
    std::string j = r.Json();
    j.pop_back();                                   // the next launch appends fields and the '}'
    const int next = 1 - g_crashHeadIdx.load();
    const size_t len = std::min(j.size(), sizeof(g_crashHead[0]) - 1);
    std::memcpy(g_crashHead[next], j.data(), len);
    g_crashHeadLen[next] = len;
    g_crashHeadIdx = next;
}

void Enqueue(const std::string& json)
{
    State& s = S();
    char name[64];
    std::snprintf(name, sizeof(name), "/%lld-%d.json", (long long)time(nullptr), std::rand() % 100000);
    const std::string tmp = s.queue + name + ".tmp", fin = s.queue + name;
    {
        std::ofstream f(tmp, std::ios::binary);
        f << json;
    }
    ::rename(tmp.c_str(), fin.c_str());
    s.wake.notify_one();
}

std::vector<std::string> QueueFiles()
{
    std::vector<std::pair<std::string, long long>> files;
    if (DIR* d = opendir(S().queue.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) {
                struct stat st;
                const std::string p = S().queue + "/" + n;
                if (::stat(p.c_str(), &st) == 0) files.emplace_back(p, (long long)st.st_size);
            }
        }
        closedir(d);
    }
    std::sort(files.begin(), files.end());                    // names start with the time: oldest first
    size_t total = 0;
    for (auto& f : files) total += (size_t)f.second;
    std::vector<std::string> out;
    for (auto& f : files) {
        if (total > kQueueCap) { ::unlink(f.first.c_str()); total -= (size_t)f.second; continue; }
        out.push_back(f.first);
    }
    return out;
}

void Notice(const std::string& text)
{
    std::lock_guard<std::mutex> l(S().mu);
    S().notices.push_back(text);
}

void Worker()
{
    State& s = S();
    for (;;) {
        bool anyFailed = false;
        for (const std::string& f : QueueFiles()) {
            const std::string body = ReadFile(f);
            const bool fromHotkey = body.find("\"type\":\"hotkey\"") != std::string::npos;
            const long status = mac::HttpPostJson(s.url, s.key, body);
            if (status >= 200 && status < 300) {
                ::unlink(f.c_str());
                if (fromHotkey) Notice("Bug report sent. Thanks!");
            } else if (status == 400 || status == 401 || status == 413) {
                ::unlink(f.c_str());   // the server will never take this one
                std::fprintf(stderr, "[telemetry] report rejected (%ld)\n", status);
            } else {
                anyFailed = true;
                if (fromHotkey) Notice("Bug report saved - it will be sent when you are online.");
                break;
            }
        }
        std::unique_lock<std::mutex> l(s.mu);
        s.wake.wait_for(l, std::chrono::seconds(anyFailed ? 120 : 600));
    }
}

void SendPendingCrash()
{
    State& s = S();
    struct stat st;
    if (::stat(s.pendingCrash.c_str(), &st) != 0) return;
    const std::string all = ReadFile(s.pendingCrash);
    ::unlink(s.pendingCrash.c_str());
    const size_t nl = all.find('\n');
    if (nl == std::string::npos || all.compare(0, 1, "{") != 0) return;
    const std::string head = all.substr(0, nl), rest = all.substr(nl + 1);
    std::string logFile;
    const size_t lf = head.find("\"log_file\":\"");
    if (lf != std::string::npos) logFile = head.substr(lf + 12, head.find('"', lf + 12) - (lf + 12));
    const char* home = std::getenv("HOME");
    // macOS writes the .ips a moment after the crash; by the next launch it is there.
    const std::string ips = NewestFile(std::string(home ? home : "") + "/Library/Logs/DiagnosticReports", "ran_client", ".ips",
                                       st.st_mtime - 30);
    const size_t sig = rest.find('\n');
    std::string json = head;
    json += ",\"message\":" + JsonString(rest.substr(0, sig));
    json += ",\"backtrace\":" + JsonString(sig == std::string::npos ? "" : rest.substr(sig + 1));
    if (!logFile.empty()) json += ",\"log_tail\":" + JsonString(TailLines(logFile, kLogTailLines));
    json += ",\"stderr_tail\":" + JsonString(TailLines(s.stderrLog + ".previous", kStderrTailLines));
    if (!ips.empty()) json += ",\"ips\":" + JsonString(ReadFile(ips).substr(0, 1500000));
    json += "}";
    Enqueue(json);
}

void BuildReport(IDirect3DDevice9* dev, const char* type, const std::string& message)
{
    State& s = S();
    Report r = Context(type);
    if (!message.empty()) r.Set("message", message);
    r.Set("log_tail", TailLines(GameLogFile(), kLogTailLines));
    r.Set("stderr_tail", TailLines(s.stderrLog, kStderrTailLines));
    std::vector<unsigned char> px;
    unsigned w = 0, h = 0;
    if (RanCaptureBackBuffer(dev, px, w, h)) {
        const std::string png = mac::EncodePng(px, w, h, 1280);
        if (!png.empty()) r.Set("screenshot_png_b64", Base64(png));
    }
    Enqueue(r.Json());
}

} // namespace

void Init()
{
    State& s = S();
    const char* off = std::getenv("RAN_TELEMETRY");
    if (off && std::strcmp(off, "0") == 0) return;
    s.key = std::getenv("RAN_INGEST_KEY") ? std::getenv("RAN_INGEST_KEY") : mac::BundleValue("RANIngestKey");
    if (s.key.empty()) return;   // a dev build without a key: no reports
    s.url = std::getenv("RAN_REPORT_URL") ? std::getenv("RAN_REPORT_URL") : kDefaultUrl;
    s.dir = mac::SupportDir() + "/reports";
    s.queue = s.dir + "/queue";
    s.pendingCrash = s.dir + "/pending-crash.txt";
    s.stderrLog = s.dir + "/stderr.log";
    MkdirP(s.queue);
    std::snprintf(g_pendingCrashPath, sizeof(g_pendingCrashPath), "%s", s.pendingCrash.c_str());

    // Launched from Finder, stderr goes nowhere: keep it (DXVK and platform messages) for reports.
    if (!isatty(2)) {
        ::rename(s.stderrLog.c_str(), (s.stderrLog + ".previous").c_str());
        if (FILE* f = std::freopen(s.stderrLog.c_str(), "w", stderr)) setvbuf(f, nullptr, _IOLBF, 0);
    }

    const std::string idFile = mac::SupportDir() + "/install-id";
    s.installId = ReadFile(idFile);
    if (s.installId.empty()) {
        char b[40];
        std::srand((unsigned)time(nullptr) ^ (unsigned)getpid());
        std::snprintf(b, sizeof(b), "%08x-%04x-%04x", (unsigned)std::rand(), (unsigned)std::rand() & 0xffff, (unsigned)std::rand() & 0xffff);
        s.installId = b;
        std::ofstream(idFile) << s.installId;
    }
    s.appVersion = mac::BundleValue("CFBundleShortVersionString");
    s.appBuild = mac::BundleValue("CFBundleVersion");
    s.macos = mac::Sysctl("kern.osproductversion");
    s.model = mac::Sysctl("hw.model");
    s.chip = mac::Sysctl("machdep.cpu.brand_string");
    s.ramGb = mac::SysctlInt("hw.memsize") >> 30;
    s.gpu = mac::GpuName();
    char cwd[2048];
    s.gameDir = getcwd(cwd, sizeof(cwd)) ? cwd : "";
    if (const char* t = std::getenv("RAN_TELEMETRY_TEST_CRASH")) s.testCrashAt = std::atof(t);

    SendPendingCrash();
    s.enabled = true;
    std::thread(Worker).detach();
}

void SetStateHook(GameState (*hook)()) { S().stateHook = hook; }
void SetNotifyHook(void (*hook)(const char*)) { S().notifyHook = hook; }

void SetAccount(const char* account)
{
    std::lock_guard<std::mutex> l(S().mu);
    S().account = account ? account : "";
}

void OnLogLine(const char* logFile, const char* line)
{
    State& s = S();
    if (!s.enabled || !line) return;
    std::lock_guard<std::mutex> l(s.mu);
    if (logFile && *logFile) s.logFile = logFile;
    if (s.pendingErrorLine.empty() && IsErrorLine(line) && s.throttle.Allow(line, Uptime())) s.pendingErrorLine = line;
}

void RequestHotkeyReport()
{
    State& s = S();
    if (!s.enabled) return;
    std::lock_guard<std::mutex> l(s.mu);
    if (Uptime() - s.lastHotkey < 10.0) return;   // a held or double-pressed key sends one report
    s.lastHotkey = Uptime();
    s.hotkeyPending = true;
}

void OnPresent(void* device)
{
    State& s = S();
    if (!s.enabled) return;
    auto* dev = (IDirect3DDevice9*)device;
    const double now = Uptime();
    if (s.testCrashAt > 0 && now > s.testCrashAt) { s.testCrashAt = -1; std::raise(SIGSEGV); }
    if (now - s.lastCrashHead > 5.0) { s.lastCrashHead = now; RefreshCrashHead(); }

    bool hotkey = false;
    std::string errorLine;
    std::deque<std::string> notices;
    {
        std::lock_guard<std::mutex> l(s.mu);
        std::swap(hotkey, s.hotkeyPending);
        std::swap(errorLine, s.pendingErrorLine);
        std::swap(notices, s.notices);
    }
    if (hotkey) {
        BuildReport(dev, "hotkey", "");
        if (s.notifyHook) s.notifyHook("Sending bug report...");
    }
    if (!errorLine.empty()) BuildReport(dev, "error", errorLine);
    for (const std::string& n : notices)
        if (s.notifyHook) s.notifyHook(n.c_str());
}

void OnFatalSignal(int sig)
{
    if (!g_pendingCrashPath[0]) return;
    const int fd = ::open(g_pendingCrashPath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return;
    const int idx = g_crashHeadIdx.load();
    const size_t len = g_crashHeadLen[idx].load();
    if (len) ::write(fd, g_crashHead[idx], len);
    else ::write(fd, "{\"type\":\"crash\"", 15);
    char line[64];
    // signal-safe formatting of the signal number
    const char* msg = "\nsignal ";
    ::write(fd, msg, std::strlen(msg));
    int n = sig, i = 0;
    char digits[12];
    do { digits[i++] = char('0' + n % 10); n /= 10; } while (n && i < 11);
    while (i) line[0] = digits[--i], ::write(fd, line, 1);
    ::write(fd, "\n", 1);
    void* frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fd);
    ::close(fd);
}

} // namespace ran_telemetry

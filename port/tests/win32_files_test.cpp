// Behaviour tests for path resolution and the Win32/CRT file stand-ins.
#include <windows.h>
#include "win32/kernel.h"
#include "win32/files.h"
#include "mfc/afx_all.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

namespace fs = std::filesystem;

static void WriteText(const fs::path& p, const char* text)
{
    FILE* f = std::fopen(p.c_str(), "wb");
    std::fputs(text, f);
    std::fclose(f);
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "ran_files_test";
    fs::remove_all(root);
    fs::create_directories(root / "Data" / "Textures");
    WriteText(root / "Data" / "Textures" / "Sky.dds", "sky");

    const std::string r = root.string();

    // Backslashes become slashes; existing paths resolve to the on-disk spelling.
    std::string got = ran_compat::ResolvePath((r + "\\Data\\Textures\\Sky.dds").c_str());
    CHECK(got == r + "/Data/Textures/Sky.dds");

    // Wrong case in every component: the on-disk walk returns the real spelling on any volume
    // (this is what case-sensitive volumes rely on)...
    got = ran_compat::ResolvePathOnDisk(ran_compat::NormalizeSlashes((r + "\\data\\TEXTURES\\sky.DDS").c_str()));
    CHECK(got == r + "/Data/Textures/Sky.dds");
    // ...and ResolvePath always lands on the real file.
    got = ran_compat::ResolvePath((r + "\\data\\TEXTURES\\sky.DDS").c_str());
    CHECK(fs::equivalent(got, root / "Data" / "Textures" / "Sky.dds"));

    // Doubled separators collapse.
    got = ran_compat::ResolvePath((r + "\\\\Data//Textures\\Sky.dds").c_str());
    CHECK(got == r + "/Data/Textures/Sky.dds");

    // A file that does not exist yet keeps the resolved directories + the given leaf name.
    got = ran_compat::ResolvePathOnDisk(ran_compat::NormalizeSlashes((r + "\\data\\textures\\New.txt").c_str()));
    CHECK(got == r + "/Data/Textures/New.txt");

    // fopen_s / fopen read through the resolver.
    FILE* f = nullptr;
    CHECK(fopen_s(&f, (r + "\\DATA\\textures\\SKY.dds").c_str(), "rb") == 0 && f != nullptr);
    char buf[8] = {};
    if (f) { std::fread(buf, 1, 3, f); std::fclose(f); }
    CHECK(std::strcmp(buf, "sky") == 0);
    CHECK(fopen_s(&f, (r + "\\nope\\missing.bin").c_str(), "rb") != 0 && f == nullptr);
    FILE* g = ran_compat::fopen_resolved((r + "\\Data\\Textures\\sky.dds").c_str(), "rb");
    CHECK(g != nullptr);
    if (g) std::fclose(g);

    // CreateDirectory / GetFileAttributes / DeleteFile.
    CHECK(CreateDirectory((r + "\\Data\\Cache").c_str(), nullptr));
    CHECK(fs::is_directory(root / "Data" / "Cache"));
    CHECK(GetFileAttributes((r + "\\data\\cache").c_str()) & FILE_ATTRIBUTE_DIRECTORY);
    CHECK(GetFileAttributes((r + "\\missing").c_str()) == INVALID_FILE_ATTRIBUTES);
    WriteText(root / "Data" / "Cache" / "tmp.txt", "x");
    CHECK(DeleteFile((r + "\\data\\cache\\TMP.txt").c_str()));
    CHECK(!fs::exists(root / "Data" / "Cache" / "tmp.txt"));

    // CreateFile / WriteFile / ReadFile / GetFileSize / SetFilePointer / CloseHandle.
    HANDLE h = CreateFile((r + "\\Data\\Cache\\blob.bin").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    CHECK(WriteFile(h, "hello world", 11, &written, nullptr) && written == 11);
    CloseHandle(h);
    h = CreateFile((r + "\\data\\CACHE\\blob.bin").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    CHECK(GetFileSize(h, nullptr) == 11);
    CHECK(SetFilePointer(h, 6, nullptr, FILE_BEGIN) == 6);
    char rb[8] = {};
    DWORD nread = 0;
    CHECK(ReadFile(h, rb, 5, &nread, nullptr) && nread == 5 && std::strcmp(rb, "world") == 0);
    CloseHandle(h);
    CHECK(CreateFile((r + "\\missing.bin").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) == INVALID_HANDLE_VALUE);

    // Current directory round trip.
    char cwd[1024];
    CHECK(GetCurrentDirectory(sizeof(cwd), cwd) > 0);
    CHECK(SetCurrentDirectory((r + "\\Data").c_str()));
    char now[1024];
    GetCurrentDirectory(sizeof(now), now);
    CHECK(fs::equivalent(now, root / "Data"));
    SetCurrentDirectory(cwd);

    // GetModuleFileName returns this executable's absolute path with Windows separators: the
    // game finds its folder with ReverseFind('\\') (CBasicApp::SetAppPath). ResolvePath maps it back.
    char exe[1024];
    CHECK(GetModuleFileName(nullptr, exe, sizeof(exe)) > 0 && exe[0] == '\\');
    CHECK(std::strchr(exe, '/') == nullptr);
    CHECK(ran_compat::PathExists(ran_compat::ResolvePath(exe)));
    // RAN_GAME_DIR points the game at a client data folder: the module is "<dir>\Game.exe".
    setenv("RAN_GAME_DIR", root.c_str(), 1);
    CHECK(GetModuleFileName(nullptr, exe, sizeof(exe)) > 0);
    const std::string expectExe = ran_compat::ResolvePath(exe);
    CHECK(expectExe == root.string() + "/Game.exe");
    unsetenv("RAN_GAME_DIR");

    // CFileFind over a directory with backslash pattern (screenshot-name probing in DxGrapUtils).
    WriteText(root / "Data" / "shot001.jpg", "a");
    WriteText(root / "Data" / "shot002.jpg", "b");
    CFileFind ff;
    CHECK(ff.FindFile((r + "\\Data\\shot001.jpg").c_str()));
    CHECK(!ff.FindFile((r + "\\Data\\shot999.jpg").c_str()));
    int found = 0;
    BOOL more = ff.FindFile((r + "\\Data\\*.jpg").c_str());
    while (more) {
        more = ff.FindNextFile();
        CHECK(ff.GetFileName() == "shot001.jpg" || ff.GetFileName() == "shot002.jpg");
        CHECK(!ff.IsDirectory());
        ++found;
    }
    CHECK(found == 2);
    ff.Close();

    fs::remove_all(root);
    if (g_failed == 0) std::printf("PASS win32_files_test\n");
    return g_failed == 0 ? 0 : 1;
}

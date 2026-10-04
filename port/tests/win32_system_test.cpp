// Behaviour tests for the registry (file-backed), shell folders, CRT io, path splitting,
// CryptoAPI (MD5 + RC4) and MFC CFile stand-ins.
#include "ran_compat.h"
#include <io.h>
#include <shlobj.h>
#include <wincrypt.h>
#include "mfc/afx_all.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

int main()
{
    char tmpl[] = "/tmp/ran_sys_test_XXXXXX";
    const std::string home = ::mkdtemp(tmpl);
    setenv("RAN_USER_DATA_DIR", home.c_str(), 1);

    // ---- Window rects come from the platform's client size (points); no hook = empty.
    {
        RECT r = { 1, 2, 3, 4 };
        CHECK(!GetClientRect((HWND)0x1, &r) && r.right == 0 && r.bottom == 0);
        ran_compat::ClientSizeHook() = [](HWND, SIZE* s) -> BOOL { s->cx = 1024; s->cy = 768; return TRUE; };
        CHECK(GetClientRect((HWND)0x1, &r) && r.left == 0 && r.top == 0 && r.right == 1024 && r.bottom == 768);
        CHECK(GetWindowRect((HWND)0x1, &r) && r.right == 1024);
        CHECK(!GetClientRect(nullptr, &r));
        ran_compat::ClientSizeHook() = nullptr;
    }

    // ---- Shell folders: "My Documents" is the per-user data directory, created on demand.
    char docs[MAX_PATH] = {};
    CHECK(SHGetSpecialFolderPath(nullptr, docs, CSIDL_PERSONAL, TRUE));
    CHECK(std::string(docs) == home);
    char appdata[MAX_PATH] = {};
    CHECK(SHGetSpecialFolderPath(nullptr, appdata, CSIDL_APPDATA, TRUE) && std::string(appdata) == home);

    // ---- Registry: values persist across handles and survive a reload from disk.
    HKEY key = nullptr;
    CHECK(RegCreateKeyEx(HKEY_CURRENT_USER, "Software\\RanOdyssey", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr) == ERROR_SUCCESS);
    const DWORD volume = 70;
    CHECK(RegSetValueEx(key, "Volume", 0, REG_DWORD, (const BYTE*)&volume, sizeof(volume)) == ERROR_SUCCESS);
    CHECK(RegSetValueEx(key, "Name", 0, REG_SZ, (const BYTE*)"hero", 5) == ERROR_SUCCESS);
    RegCloseKey(key);

    ran_compat::Registry::Get().Reload();   // as if the game restarted
    HKEY rk = nullptr;
    CHECK(RegOpenKeyEx(HKEY_CURRENT_USER, "software\\ranodyssey", 0, KEY_READ, &rk) == ERROR_SUCCESS);   // case-insensitive like Windows
    DWORD type = 0, got = 0, size = sizeof(got);
    CHECK(RegQueryValueEx(rk, "Volume", nullptr, &type, (BYTE*)&got, &size) == ERROR_SUCCESS && type == REG_DWORD && got == 70);
    char name[16] = {};
    DWORD nsize = sizeof(name);
    CHECK(RegQueryValueEx(rk, "Name", nullptr, &type, (BYTE*)name, &nsize) == ERROR_SUCCESS && type == REG_SZ && std::strcmp(name, "hero") == 0);
    DWORD small = 2;
    CHECK(RegQueryValueEx(rk, "Name", nullptr, &type, (BYTE*)name, &small) == ERROR_MORE_DATA && small == 5);
    CHECK(RegQueryValueEx(rk, "Missing", nullptr, &type, (BYTE*)name, &nsize) == ERROR_FILE_NOT_FOUND);
    CHECK(RegDeleteValue(rk, "Name") == ERROR_SUCCESS);
    CHECK(RegQueryValueEx(rk, "Name", nullptr, &type, (BYTE*)name, &nsize) == ERROR_FILE_NOT_FOUND);
    RegCloseKey(rk);
    HKEY none = nullptr;
    CHECK(RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Nope", 0, KEY_READ, &none) == ERROR_FILE_NOT_FOUND);

    // ---- CRT io with Windows paths.
    const std::string file = home + "\\io.bin";
    int fd = _open(file.c_str(), _O_CREAT | _O_WRONLY | _O_BINARY | _O_TRUNC, _S_IREAD | _S_IWRITE);
    CHECK(fd >= 0);
    CHECK(_write(fd, "abcdef", 6) == 6);
    _close(fd);
    CHECK(_access(file.c_str(), 0) == 0 && _access((home + "\\nope").c_str(), 0) == -1);
    fd = _open(file.c_str(), _O_RDONLY | _O_BINARY);
    CHECK(_filelength(fd) == 6);
    CHECK(_lseek(fd, 2, SEEK_SET) == 2);
    char io[4] = {};
    CHECK(_read(fd, io, 3) == 3 && std::strcmp(io, "cde") == 0);
    _close(fd);

    // ---- _splitpath / _makepath on Windows-style paths.
    char drive[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];
    _splitpath("C:\\Games\\Ran\\data\\item.isf", drive, dir, fname, ext);
    CHECK(std::strcmp(drive, "C:") == 0 && std::strcmp(dir, "\\Games\\Ran\\data\\") == 0);
    CHECK(std::strcmp(fname, "item") == 0 && std::strcmp(ext, ".isf") == 0);
    CHECK(_splitpath_s("glogic\\skill.ssf", drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT) == 0);
    CHECK(drive[0] == 0 && std::strcmp(dir, "glogic\\") == 0 && std::strcmp(fname, "skill") == 0 && std::strcmp(ext, ".ssf") == 0);
    char made[MAX_PATH];
    _makepath(made, nullptr, "data\\", "map", ".lev");
    CHECK(std::strcmp(made, "data\\map.lev") == 0);

    // ---- CryptoAPI subset: MD5-derived RC4 key, encrypt/decrypt round trip (RANPARAM user id).
    const char* keyText = "ran-key";
    const char plain[] = "player01";
    BYTE buf[16];
    std::memcpy(buf, plain, sizeof(plain));
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    HCRYPTKEY ck = 0;
    CHECK(CryptAcquireContext(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT));
    CHECK(CryptCreateHash(prov, CALG_MD5, 0, 0, &hash));
    CHECK(CryptHashData(hash, (const BYTE*)keyText, (DWORD)std::strlen(keyText), 0));
    CHECK(CryptDeriveKey(prov, CALG_RC4, hash, CRYPT_EXPORTABLE, &ck));
    DWORD len = sizeof(plain);
    CHECK(CryptEncrypt(ck, 0, TRUE, 0, buf, &len, sizeof(buf)));
    CHECK(std::memcmp(buf, plain, sizeof(plain)) != 0);
    CryptDestroyKey(ck);
    // RC4 is a stream cipher: a fresh key from the same hash decrypts.
    CHECK(CryptDeriveKey(prov, CALG_RC4, hash, CRYPT_EXPORTABLE, &ck));
    CHECK(CryptDecrypt(ck, 0, TRUE, 0, buf, &len));
    CHECK(std::memcmp(buf, plain, sizeof(plain)) == 0);
    CryptDestroyKey(ck);
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);

    // ---- MFC CFile.
    CFile cf;
    CHECK(cf.Open((home + "\\cfile.dat").c_str(), CFile::modeCreate | CFile::modeWrite | CFile::typeBinary));
    cf.Write("12345", 5);
    cf.Close();
    CHECK(cf.Open((home + "\\cfile.dat").c_str(), CFile::modeRead | CFile::typeBinary));
    CHECK(cf.GetLength() == 5);
    cf.Seek(1, CFile::begin);
    char cb[4] = {};
    CHECK(cf.Read(cb, 3) == 3 && std::strcmp(cb, "234") == 0);
    cf.Close();

    // Clean up the temp tree.
    std::string rm = "rm -rf '" + home + "'";
    (void)std::system(rm.c_str());
    if (g_failed == 0) std::printf("PASS win32_system_test\n");
    return g_failed == 0 ? 0 : 1;
}

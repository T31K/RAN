// Windows registry for the native macOS build: a small key/value store persisted to
// <user data dir>/registry.txt. Key paths and value names are case-insensitive, like Windows.
// The game only keeps a handful of settings here (DXUT/SDK paths, a few client options).
#pragma once
#include "win32/kernel.h"
#include "win32/userdata.h"
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

typedef HKEY* PHKEY;   // HKEY itself is an opaque HANDLE in DXVK's windows_base.h
typedef DWORD REGSAM;
typedef LONG LSTATUS;

#define HKEY_CLASSES_ROOT     ((HKEY)(uintptr_t)0x80000000)
#define HKEY_CURRENT_USER     ((HKEY)(uintptr_t)0x80000001)
#define HKEY_LOCAL_MACHINE    ((HKEY)(uintptr_t)0x80000002)
#define HKEY_USERS            ((HKEY)(uintptr_t)0x80000003)
#define HKEY_CURRENT_CONFIG   ((HKEY)(uintptr_t)0x80000005)

#define KEY_QUERY_VALUE       0x0001
#define KEY_SET_VALUE         0x0002
#define KEY_CREATE_SUB_KEY    0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_READ              0x20019
#define KEY_WRITE             0x20006
#define KEY_ALL_ACCESS        0xF003F
#define REG_OPTION_NON_VOLATILE 0x0000

#define REG_NONE              0
#define REG_SZ                1
#define REG_EXPAND_SZ         2
#define REG_BINARY            3
#define REG_DWORD             4
#define REG_MULTI_SZ          7
#define REG_QWORD             11

#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND  2u
#endif
#ifndef ERROR_MORE_DATA
#define ERROR_MORE_DATA       234u
#endif
#ifndef ERROR_INVALID_HANDLE
#define ERROR_INVALID_HANDLE  6u
#endif

namespace ran_compat {

    class Registry {
    public:
        struct Value { DWORD type = REG_NONE; std::vector<BYTE> data; };

        static Registry& Get() { static Registry r; return r; }

        // Opened keys are heap objects holding the full lower-cased path.
        struct Handle { std::string path; };

        static std::string Lower(std::string s)
        {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        std::string RootName(HKEY root) const
        {
            if (root == HKEY_CLASSES_ROOT) return "hkcr";
            if (root == HKEY_CURRENT_USER) return "hkcu";
            if (root == HKEY_LOCAL_MACHINE) return "hklm";
            if (root == HKEY_USERS) return "hku";
            if (root == HKEY_CURRENT_CONFIG) return "hkcc";
            return root ? ((Handle*)root)->path : std::string();
        }

        std::string Join(HKEY parent, const char* sub) const
        {
            std::string p = RootName(parent);
            if (sub && *sub) {
                std::string s = sub;
                for (char& c : s) if (c == '/') c = '\\';
                while (!s.empty() && s.back() == '\\') s.pop_back();
                p += "\\" + s;
            }
            return Lower(p);
        }

        bool KeyExists(const std::string& path)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Load();
            return m_keys.count(path) != 0;
        }

        void CreateKey(const std::string& path)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Load();
            m_keys[path];
            Save();
        }

        bool Query(const std::string& path, const std::string& name, Value& out)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Load();
            auto k = m_keys.find(path);
            if (k == m_keys.end()) return false;
            auto v = k->second.find(Lower(name));
            if (v == k->second.end()) return false;
            out = v->second;
            return true;
        }

        void Set(const std::string& path, const std::string& name, const Value& v)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Load();
            m_keys[path][Lower(name)] = v;
            Save();
        }

        bool Delete(const std::string& path, const std::string& name)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Load();
            auto k = m_keys.find(path);
            if (k == m_keys.end() || k->second.erase(Lower(name)) == 0) return false;
            Save();
            return true;
        }

        // Drops the in-memory copy; the next access reads the file again.
        void Reload() { std::lock_guard<std::mutex> lock(m_mutex); m_loaded = false; m_keys.clear(); }

    private:
        std::string FilePath() const { return UserDataDir() + "/registry.txt"; }

        // File format, one line per value (keys without values keep a line with an empty name):
        //   <key path>\t<value name>\t<type>\t<hex bytes>
        void Load()
        {
            if (m_loaded) return;
            m_loaded = true;
            std::ifstream in(FilePath());
            std::string line;
            while (std::getline(in, line)) {
                std::stringstream ss(line);
                std::string path, name, type, hex;
                std::getline(ss, path, '\t');
                std::getline(ss, name, '\t');
                std::getline(ss, type, '\t');
                std::getline(ss, hex, '\t');
                auto& values = m_keys[path];
                if (name.empty() && type.empty()) continue;
                Value v;
                v.type = (DWORD)std::strtoul(type.c_str(), nullptr, 10);
                for (size_t i = 0; i + 1 < hex.size(); i += 2) v.data.push_back((BYTE)std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
                values[name] = v;
            }
        }

        void Save()
        {
            std::ofstream out(FilePath(), std::ios::trunc);
            static const char* digits = "0123456789abcdef";
            for (const auto& k : m_keys) {
                if (k.second.empty()) { out << k.first << "\t\t\t\n"; continue; }
                for (const auto& v : k.second) {
                    out << k.first << '\t' << v.first << '\t' << v.second.type << '\t';
                    for (BYTE b : v.second.data) out << digits[b >> 4] << digits[b & 15];
                    out << '\n';
                }
            }
        }

        std::mutex m_mutex;
        bool m_loaded = false;
        std::map<std::string, std::map<std::string, Value>> m_keys;
    };

    inline HKEY MakeKeyHandle(const std::string& path) { return (HKEY)new Registry::Handle{path}; }
    inline bool IsPredefinedKey(HKEY k) { return (uintptr_t)k >= 0x80000000u && (uintptr_t)k <= 0x80000006u; }
}

inline LONG RegOpenKeyEx(HKEY parent, const char* sub, DWORD, REGSAM, PHKEY out)
{
    auto& reg = ran_compat::Registry::Get();
    const std::string path = reg.Join(parent, sub);
    if (!reg.KeyExists(path)) return ERROR_FILE_NOT_FOUND;
    if (out) *out = ran_compat::MakeKeyHandle(path);
    return ERROR_SUCCESS;
}
#define RegOpenKeyExA RegOpenKeyEx
inline LONG RegOpenKey(HKEY parent, const char* sub, PHKEY out) { return RegOpenKeyEx(parent, sub, 0, KEY_ALL_ACCESS, out); }

inline LONG RegCreateKeyEx(HKEY parent, const char* sub, DWORD, char*, DWORD, REGSAM, SECURITY_ATTRIBUTES*, PHKEY out, LPDWORD disposition)
{
    auto& reg = ran_compat::Registry::Get();
    const std::string path = reg.Join(parent, sub);
    const bool existed = reg.KeyExists(path);
    if (!existed) reg.CreateKey(path);
    if (disposition) *disposition = existed ? 2u /*REG_OPENED_EXISTING_KEY*/ : 1u /*REG_CREATED_NEW_KEY*/;
    if (out) *out = ran_compat::MakeKeyHandle(path);
    return ERROR_SUCCESS;
}
#define RegCreateKeyExA RegCreateKeyEx
inline LONG RegCreateKey(HKEY parent, const char* sub, PHKEY out) { return RegCreateKeyEx(parent, sub, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, out, nullptr); }

inline LONG RegCloseKey(HKEY key)
{
    if (!key || ran_compat::IsPredefinedKey(key)) return ERROR_SUCCESS;
    delete (ran_compat::Registry::Handle*)key;
    return ERROR_SUCCESS;
}

inline LONG RegQueryValueEx(HKEY key, const char* name, LPDWORD, LPDWORD type, BYTE* data, LPDWORD size)
{
    auto& reg = ran_compat::Registry::Get();
    ran_compat::Registry::Value v;
    if (!reg.Query(reg.Join(key, nullptr), name ? name : "", v)) return ERROR_FILE_NOT_FOUND;
    if (type) *type = v.type;
    const DWORD need = (DWORD)v.data.size();
    if (!size) return data ? (LONG)ERROR_INVALID_HANDLE : (LONG)ERROR_SUCCESS;
    if (!data) { *size = need; return ERROR_SUCCESS; }
    if (*size < need) { *size = need; return ERROR_MORE_DATA; }
    std::memcpy(data, v.data.data(), need);
    *size = need;
    return ERROR_SUCCESS;
}
#define RegQueryValueExA RegQueryValueEx

inline LONG RegSetValueEx(HKEY key, const char* name, DWORD, DWORD type, const BYTE* data, DWORD size)
{
    auto& reg = ran_compat::Registry::Get();
    ran_compat::Registry::Value v;
    v.type = type;
    if (data && size) v.data.assign(data, data + size);
    reg.Set(reg.Join(key, nullptr), name ? name : "", v);
    return ERROR_SUCCESS;
}
#define RegSetValueExA RegSetValueEx

inline LONG RegDeleteValue(HKEY key, const char* name)
{
    auto& reg = ran_compat::Registry::Get();
    return reg.Delete(reg.Join(key, nullptr), name ? name : "") ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}
#define RegDeleteValueA RegDeleteValue

// Wide variants: names converted to UTF-8 bytes (ASCII in practice - DXUT registry paths).
namespace ran_compat {
    inline std::string NarrowAscii(const WCHAR* w)
    {
        std::string s;
        if (w) for (; *w; ++w) s += (char)(*w < 0x80 ? *w : '?');
        return s;
    }
}
inline LONG RegOpenKeyExW(HKEY parent, const WCHAR* sub, DWORD o, REGSAM sam, PHKEY out)
{
    return RegOpenKeyEx(parent, ran_compat::NarrowAscii(sub).c_str(), o, sam, out);
}
inline LONG RegQueryValueExW(HKEY key, const WCHAR* name, LPDWORD r, LPDWORD type, BYTE* data, LPDWORD size)
{
    return RegQueryValueEx(key, ran_compat::NarrowAscii(name).c_str(), r, type, data, size);
}
inline LONG RegSetValueExW(HKEY key, const WCHAR* name, DWORD r, DWORD type, const BYTE* data, DWORD size)
{
    return RegSetValueEx(key, ran_compat::NarrowAscii(name).c_str(), r, type, data, size);
}

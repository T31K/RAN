// The game folder in the browser: a WasmFS backend over the Origin Private File System.
//
// The page unpacks the client archive into OPFS once (loader.js / unpack-worker.js) and keeps
// an index of every file and its size. At startup this backend builds the directory tree from
// that index (no file contents are read), so stat/readdir/ResolvePath work at once. A file's
// bytes are fetched from OPFS on its first read - an async JS call that suspends the calling
// green thread (JSPI) - and kept while the file is open (small files stay cached). Writes stay
// in memory and are stored back to OPFS when the file is closed or flushed.
//
// Lookups are case-insensitive like Windows (exact spelling first), so game paths written in
// any case reach the archive's files without per-component directory scans.
#include "backend.h"
#include "file.h"
#include "wasmfs.h"
#include "ran_green.h"
#include <emscripten.h>
#include <algorithm>
#include <list>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

extern "C" {
// gamefs.js
char* ran_gamefs_index();                                            // "path\tsize\n"..., malloc'd
int ran_gamefs_load(const char* path, uint8_t* dst, int size);       // async; 0 = ok
int ran_gamefs_store(const char* path, const uint8_t* src, int len); // async; 0 = ok
void ran_gamefs_remove(const char* path);                            // fire and forget
// WasmFS (emscripten/wasmfs.h declares it with its own opaque Backend type)
int wasmfs_create_directory(const char* path, mode_t mode, wasmfs::backend_t backend);
}

namespace {

using namespace wasmfs;

backend_t g_backend = nullptr;
size_t g_cachedBytes = 0;
// Closed files keep their bytes, least recently used dropped first beyond this budget: the game
// reopens its archives (.rcc zip files) for every entry it reads.
const size_t kCacheBudget = 256u << 20;

class GameFile;
std::list<GameFile*>& Lru() { static std::list<GameFile*> l; return l; }   // front = most recent

class GameFile : public DataFile {
public:
    GameFile(std::string path, off_t size, bool fresh)
        : DataFile(S_IFREG | 0666, g_backend), path(std::move(path)), size(size), loaded(fresh), dirty(fresh) {}

    std::string path;    // relative to the OPFS game folder, '/' separated, on-disk spelling

private:
    off_t size;
    std::vector<uint8_t> data;
    bool loaded;
    bool loading = false;
    bool dirty;
    bool pinned = false;
    int opens = 0;

    // Brings the contents in from OPFS. Other green threads asking meanwhile wait for the load.
    bool Ensure()
    {
        while (loading) ran_green::Block(50);
        if (loaded) return true;
        loading = true;
        data.resize((size_t)size);
        int rc = 0;
        if (size) {
            ran_green::AsyncScope scope;   // other threads run while this one waits for the file
            rc = ran_gamefs_load(path.c_str(), data.data(), (int)size);
        }
        loading = false;
        ran_green::WakeAll();   // threads waiting for this file
        if (rc != 0) {
            std::fprintf(stderr, "[gamefs] cannot read %s\n", path.c_str());
            data.clear();
            data.shrink_to_fit();
            return false;
        }
        loaded = true;
        g_cachedBytes += data.size();
        return true;
    }

    void Store()
    {
        if (!dirty || !loaded) return;
        dirty = false;   // cleared first: a write during the store marks it dirty again
        int rc;
        {
            ran_green::AsyncScope scope;
            rc = ran_gamefs_store(path.c_str(), data.data(), (int)data.size());
        }
        if (rc == 2) pinned = true;   // not stored (development mode): the memory copy is the file
        else if (rc != 0) std::fprintf(stderr, "[gamefs] cannot store %s\n", path.c_str());
    }

    bool inLru = false;
    std::list<GameFile*>::iterator lruPos;

    void LeaveLru()
    {
        if (!inLru) return;
        Lru().erase(lruPos);
        inLru = false;
    }

    void Drop()
    {
        LeaveLru();
        g_cachedBytes -= data.size();
        size = (off_t)data.size();
        data.clear();
        data.shrink_to_fit();
        loaded = false;
    }

    // A closed, clean file becomes the most recently used cache entry; the oldest entries are
    // dropped while the cache is over budget.
    void Release()
    {
        if (!loaded || dirty || opens > 0 || loading || pinned) return;
        LeaveLru();
        Lru().push_front(this);
        lruPos = Lru().begin();
        inLru = true;
        while (g_cachedBytes > kCacheBudget && !Lru().empty()) Lru().back()->Drop();
    }

    int open(oflags_t) override { ++opens; LeaveLru(); return 0; }
    int close() override
    {
        if (opens > 0) --opens;
        Store();
        Release();
        return 0;
    }
    ssize_t read(uint8_t* buf, size_t len, off_t offset) override
    {
        if (!Ensure()) return -EIO;
        if ((size_t)offset >= data.size()) return 0;
        len = std::min(len, data.size() - (size_t)offset);
        std::memcpy(buf, data.data() + offset, len);
        return (ssize_t)len;
    }
    ssize_t write(const uint8_t* buf, size_t len, off_t offset) override
    {
        if (!Ensure()) return -EIO;
        if (std::getenv("RAN_FS_TRACE") && data.size() < (1u << 20) && (uint64_t)offset + len >= (1u << 20))
            emscripten_log(EM_LOG_CONSOLE | EM_LOG_C_STACK, "[gamefs] %s passes 1 MB", path.c_str());
        if ((uint64_t)offset + len > (64u << 20) && (uint64_t)offset + len > data.size()) {
            static int reported = 0;
            if (reported++ < 3)
                emscripten_log(EM_LOG_CONSOLE | EM_LOG_ERROR | EM_LOG_C_STACK, "[gamefs] refusing to grow %s to %llu bytes (write of %zu at %lld)",
                               path.c_str(), (unsigned long long)offset + len, len, (long long)offset);
            return -EFBIG;
        }
        if ((size_t)offset + len > data.size()) {
            g_cachedBytes += (size_t)offset + len - data.size();
            data.resize((size_t)offset + len);
        }
        std::memcpy(data.data() + offset, buf, len);
        size = (off_t)data.size();
        dirty = true;
        return (ssize_t)len;
    }
    int setSize(off_t newSize) override
    {
        if (newSize == 0 && !loading) {   // truncate on open for writing: no need to load
            if (loaded) g_cachedBytes -= data.size();
            data.clear();
            loaded = true;
        } else if (!Ensure()) {
            return -EIO;
        }
        if (loaded && (size_t)newSize > data.size()) g_cachedBytes += (size_t)newSize - data.size();
        data.resize((size_t)newSize);
        size = newSize;
        dirty = true;
        return 0;
    }
    int flush() override { Store(); return 0; }
    off_t getSize() override { return loaded ? (off_t)data.size() : size; }
};

class GameDir : public Directory {
public:
    explicit GameDir(std::string path) : Directory(S_IFDIR | 0777, g_backend), path(std::move(path)) {}

    std::string path;   // "" for the root, else "a/b/" (with the trailing slash)

    struct Child { std::string name; std::shared_ptr<File> file; };
    std::vector<Child> children;

    std::shared_ptr<GameDir> SubDir(const std::string& name)
    {
        for (auto& c : children)
            if (c.name == name && c.file->is<Directory>()) return std::static_pointer_cast<GameDir>(c.file);
        auto d = std::make_shared<GameDir>(path + name + "/");
        Attach(name, d);
        return d;
    }
    void Attach(const std::string& name, std::shared_ptr<File> f)
    {
        children.push_back({name, f});
        f->locked().setParent(std::static_pointer_cast<Directory>(shared_from_this()));
    }

private:
    std::vector<Child>::iterator Find(const std::string& name)
    {
        auto it = std::find_if(children.begin(), children.end(), [&](const Child& c) { return c.name == name; });
        if (it != children.end()) return it;
        return std::find_if(children.begin(), children.end(),
                            [&](const Child& c) { return strcasecmp(c.name.c_str(), name.c_str()) == 0; });
    }

    std::shared_ptr<File> getChild(const std::string& name) override
    {
        auto it = Find(name);
        return it == children.end() ? nullptr : it->file;
    }
    std::shared_ptr<DataFile> insertDataFile(const std::string& name, mode_t) override
    {
        auto f = std::make_shared<GameFile>(path + name, 0, true);
        children.push_back({name, f});
        return f;
    }
    std::shared_ptr<Directory> insertDirectory(const std::string& name, mode_t) override
    {
        auto d = std::make_shared<GameDir>(path + name + "/");
        children.push_back({name, d});
        return d;
    }
    std::shared_ptr<Symlink> insertSymlink(const std::string&, const std::string&) override { return nullptr; }
    int insertMove(const std::string& name, std::shared_ptr<File> file) override
    {
        // Rename: move the entry, then store the contents under the new name.
        auto oldParent = std::static_pointer_cast<GameDir>(file->locked().getParent());
        if (oldParent) {
            auto& v = oldParent->children;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Child& c) { return c.file == file; }), v.end());
        }
        (void)removeChild(name);
        children.push_back({name, file});
        if (auto g = file->dynCast<DataFile>()) {
            auto gf = std::static_pointer_cast<GameFile>(g);
            const std::string old = gf->path;
            gf->path = path + name;
            uint8_t probe;
            gf->locked().read(&probe, 0, 0);                 // load under the old name
            gf->locked().setSize(gf->locked().getSize());    // mark dirty
            gf->locked().flush();
            ran_gamefs_remove(old.c_str());
        }
        return 0;
    }
    int removeChild(const std::string& name) override
    {
        auto it = Find(name);
        if (it == children.end()) return 0;
        if (auto f = it->file->dynCast<DataFile>()) ran_gamefs_remove(std::static_pointer_cast<GameFile>(f)->path.c_str());
        it->file->locked().setParent(nullptr);
        children.erase(it);
        return 0;
    }
    ssize_t getNumEntries() override { return (ssize_t)children.size(); }
    MaybeEntries getEntries() override
    {
        std::vector<Entry> out;
        out.reserve(children.size());
        for (auto& c : children) out.push_back({c.name, c.file->kind, c.file->getIno()});
        return {out};
    }
    std::string getName(std::shared_ptr<File> file) override
    {
        for (auto& c : children)
            if (c.file == file) return c.name;
        return "";
    }
    bool maintainsFileIdentity() override { return true; }
    off_t getSize() override { return 4096; }
};

class GameBackend : public Backend {
public:
    std::shared_ptr<GameDir> root;
    std::shared_ptr<DataFile> createFile(mode_t) override { return std::make_shared<GameFile>("", 0, true); }
    std::shared_ptr<Directory> createDirectory(mode_t) override
    {
        if (!root) { root = std::make_shared<GameDir>(""); return root; }
        return std::make_shared<GameDir>("");
    }
    std::shared_ptr<Symlink> createSymlink(std::string) override { return nullptr; }
};

} // namespace

// Mounts the game folder at `mountPoint` from the page's file index. Returns 0 on success.
extern "C" int ran_web_mount_game(const char* mountPoint)
{
    static GameBackend* backend = new GameBackend();
    g_backend = (backend_t)backend;
    if (wasmfs_create_directory(mountPoint, 0777, (backend_t)backend) != 0) return -1;
    char* index = ran_gamefs_index();
    if (!index) return -1;
    size_t files = 0;
    for (char* line = index; *line;) {
        char* end = std::strchr(line, '\n');
        if (end) *end = 0;
        char* tab = std::strchr(line, '\t');
        if (tab) {
            *tab = 0;
            const off_t size = (off_t)std::strtoll(tab + 1, nullptr, 10);
            std::shared_ptr<GameDir> dir = backend->root;
            std::string rel = line;
            size_t start = 0;
            for (size_t slash; (slash = rel.find('/', start)) != std::string::npos; start = slash + 1)
                dir = dir->SubDir(rel.substr(start, slash - start));
            dir->Attach(rel.substr(start), std::make_shared<GameFile>(rel, size, false));
            ++files;
        }
        if (!end) break;
        line = end + 1;
    }
    std::free(index);
    std::printf("[gamefs] %zu files mounted at %s\n", files, mountPoint);
    return files ? 0 : -1;
}

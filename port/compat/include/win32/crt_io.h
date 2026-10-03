// MSVC low-level CRT file I/O (<io.h>, <fcntl.h> _O_* names, <share.h>) on POSIX, with Windows
// paths resolved through ran_compat::ResolvePath. _O_BINARY/_O_TEXT are no-ops (no CRLF
// translation on macOS; the game opens its data files binary anyway).
#pragma once
#include "win32/files.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define _O_RDONLY  O_RDONLY
#define _O_WRONLY  O_WRONLY
#define _O_RDWR    O_RDWR
#define _O_APPEND  O_APPEND
#define _O_CREAT   O_CREAT
#define _O_TRUNC   O_TRUNC
#define _O_EXCL    O_EXCL
#define _O_BINARY  0
#define _O_TEXT    0
#define _O_SEQUENTIAL 0
#define _O_RANDOM  0
#define _S_IREAD   S_IRUSR
#define _S_IWRITE  S_IWUSR
#define _S_IFDIR   S_IFDIR
#define _S_IFREG   S_IFREG
#define _SH_DENYRW 0x10
#define _SH_DENYWR 0x20
#define _SH_DENYRD 0x30
#define _SH_DENYNO 0x40

inline int _open(const char* path, int flags, int mode = 0644)
{
    return ::open(ran_compat::ResolvePath(path).c_str(), flags, (flags & O_CREAT) ? (mode | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) : 0);
}
inline int _sopen(const char* path, int flags, int /*share*/, int mode = 0644) { return _open(path, flags, mode); }
inline errno_t _sopen_s(int* fd, const char* path, int flags, int share, int mode)
{
    *fd = _sopen(path, flags, share, mode);
    return *fd >= 0 ? 0 : errno;
}
inline int _close(int fd) { return ::close(fd); }
inline int _read(int fd, void* buf, unsigned n) { return (int)::read(fd, buf, n); }
inline int _write(int fd, const void* buf, unsigned n) { return (int)::write(fd, buf, n); }
inline long _lseek(int fd, long off, int whence) { return (long)::lseek(fd, off, whence); }
inline long _tell(int fd) { return (long)::lseek(fd, 0, SEEK_CUR); }
inline long _filelength(int fd)
{
    struct stat st;
    return ::fstat(fd, &st) == 0 ? (long)st.st_size : -1;
}
inline int _access(const char* path, int mode) { return ::access(ran_compat::ResolvePath(path).c_str(), mode & 6); }
inline int _unlink(const char* path) { return ::unlink(ran_compat::ResolvePath(path).c_str()); }
inline int _mkdir(const char* path) { return ::mkdir(ran_compat::ResolvePath(path).c_str(), 0755); }
inline int _rmdir(const char* path) { return ::rmdir(ran_compat::ResolvePath(path).c_str()); }
inline int _chdir(const char* path) { return ::chdir(ran_compat::ResolvePath(path).c_str()); }
inline char* _getcwd(char* buf, int size) { return ::getcwd(buf, (size_t)size); }
inline int _getpid() { return (int)::getpid(); }
#define _fileno fileno
#define _stat stat
#define _fstat fstat

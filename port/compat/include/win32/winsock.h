// Winsock on BSD sockets for the native macOS build.
//
// The game's NetClient uses Winsock's event model: WSAEventSelect ties a socket to an event,
// a thread waits on that event (WaitForMultipleObjects / WSAWaitForMultipleEvents), and
// WSAEnumNetworkEvents says what happened. Here one watcher thread poll()s every registered
// socket and signals its event with Windows' semantics:
//   FD_CONNECT  once, when a non-blocking connect finishes (iErrorCode carries the failure)
//   FD_WRITE    after connect / after selecting on a connected socket, and again whenever the
//               socket becomes writable after having been full
//   FD_READ     while unread data is waiting (re-armed after each WSAEnumNetworkEvents)
//   FD_CLOSE    once, when the peer closes or resets the connection
#pragma once
#include "win32/extra_types.h"
#include "win32/kernel.h"
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <map>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

typedef UINT_PTR         SOCKET;
typedef HANDLE           WSAEVENT;
typedef WSAEVENT*        LPWSAEVENT;
typedef unsigned long    u_long;
typedef struct sockaddr  SOCKADDR, *PSOCKADDR, *LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;
typedef struct in_addr   IN_ADDR, *PIN_ADDR, *LPIN_ADDR;
typedef struct hostent   HOSTENT, *PHOSTENT, *LPHOSTENT;
typedef struct linger    LINGER;

#define INVALID_SOCKET   ((SOCKET)(~0))
#define SOCKET_ERROR     (-1)
#define SD_RECEIVE       SHUT_RD
#define SD_SEND          SHUT_WR
#define SD_BOTH          SHUT_RDWR

#define FD_READ_BIT      0
#define FD_WRITE_BIT     1
#define FD_OOB_BIT       2
#define FD_ACCEPT_BIT    3
#define FD_CONNECT_BIT   4
#define FD_CLOSE_BIT     5
#define FD_MAX_EVENTS    10
#define FD_READ          (1 << FD_READ_BIT)
#define FD_WRITE         (1 << FD_WRITE_BIT)
#define FD_OOB           (1 << FD_OOB_BIT)
#define FD_ACCEPT        (1 << FD_ACCEPT_BIT)
#define FD_CONNECT       (1 << FD_CONNECT_BIT)
#define FD_CLOSE         (1 << FD_CLOSE_BIT)

#define WSA_WAIT_EVENT_0    WAIT_OBJECT_0
#define WSA_WAIT_TIMEOUT    WAIT_TIMEOUT
#define WSA_WAIT_FAILED     WAIT_FAILED
#define WSA_INFINITE        INFINITE
#define WSA_INVALID_EVENT   ((WSAEVENT)nullptr)
#define WSADESCRIPTION_LEN  256
#define WSASYS_STATUS_LEN   128

// Winsock error codes (WinError.h values).
#define WSABASEERR              10000
#define WSAEINTR                10004
#define WSAEBADF                10009
#define WSAEACCES               10013
#define WSAEFAULT               10014
#define WSAEINVAL               10022
#define WSAEMFILE               10024
#define WSAEWOULDBLOCK          10035
#define WSAEINPROGRESS          10036
#define WSAEALREADY             10037
#define WSAENOTSOCK             10038
#define WSAEDESTADDRREQ         10039
#define WSAEMSGSIZE             10040
#define WSAEPROTOTYPE           10041
#define WSAENOPROTOOPT          10042
#define WSAEPROTONOSUPPORT      10043
#define WSAESOCKTNOSUPPORT      10044
#define WSAEOPNOTSUPP           10045
#define WSAEPFNOSUPPORT         10046
#define WSAEAFNOSUPPORT         10047
#define WSAEADDRINUSE           10048
#define WSAEADDRNOTAVAIL        10049
#define WSAENETDOWN             10050
#define WSAENETUNREACH          10051
#define WSAENETRESET            10052
#define WSAECONNABORTED         10053
#define WSAECONNRESET           10054
#define WSAENOBUFS              10055
#define WSAEISCONN              10056
#define WSAENOTCONN             10057
#define WSAESHUTDOWN            10058
#define WSAETOOMANYREFS         10059
#define WSAETIMEDOUT            10060
#define WSAECONNREFUSED         10061
#define WSAELOOP                10062
#define WSAENAMETOOLONG         10063
#define WSAEHOSTDOWN            10064
#define WSAEHOSTUNREACH         10065
#define WSAENOTEMPTY            10066
#define WSAEPROCLIM             10067
#define WSAEUSERS               10068
#define WSAEDQUOT               10069
#define WSAESTALE               10070
#define WSAEREMOTE              10071
#define WSASYSNOTREADY          10091
#define WSAVERNOTSUPPORTED      10092
#define WSANOTINITIALISED       10093
#define WSAEDISCON              10101
#define WSAENOMORE              10102
#define WSAECANCELLED           10103
#define WSAEINVALIDPROCTABLE    10104
#define WSAEINVALIDPROVIDER     10105
#define WSAEPROVIDERFAILEDINIT  10106
#define WSASYSCALLFAILURE       10107
#define WSASERVICE_NOT_FOUND    10108
#define WSATYPE_NOT_FOUND       10109
#define WSA_E_NO_MORE           10110
#define WSA_E_CANCELLED         10111
#define WSAEREFUSED             10112
#define WSAHOST_NOT_FOUND       11001
#define WSATRY_AGAIN            11002
#define WSANO_RECOVERY          11003
#define WSANO_DATA              11004

typedef struct WSAData {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[WSADESCRIPTION_LEN + 1];
    char szSystemStatus[WSASYS_STATUS_LEN + 1];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char* lpVendorInfo;
} WSADATA, *LPWSADATA;

typedef struct _WSANETWORKEVENTS {
    long lNetworkEvents;
    int  iErrorCode[FD_MAX_EVENTS];
} WSANETWORKEVENTS, *LPWSANETWORKEVENTS;

typedef struct _WSABUF {
    ULONG len;
    char* buf;
} WSABUF, *LPWSABUF;

typedef struct _WSAOVERLAPPED {
    ULONG_PTR Internal, InternalHigh;
    DWORD Offset, OffsetHigh;
    WSAEVENT hEvent;
} WSAOVERLAPPED, *LPWSAOVERLAPPED;

namespace ran_compat {

    inline int ErrnoToWsa(int e)
    {
        switch (e) {
        case 0:            return 0;
        case EINTR:        return WSAEINTR;
        case EBADF:        return WSAEBADF;
        case EACCES:       return WSAEACCES;
        case EFAULT:       return WSAEFAULT;
        case EINVAL:       return WSAEINVAL;
        case EMFILE:       return WSAEMFILE;
        case EWOULDBLOCK:  return WSAEWOULDBLOCK;   // == EAGAIN on macOS
        case EINPROGRESS:  return WSAEWOULDBLOCK;   // Windows reports a pending connect as WOULDBLOCK
        case EALREADY:     return WSAEALREADY;
        case ENOTSOCK:     return WSAENOTSOCK;
        case EDESTADDRREQ: return WSAEDESTADDRREQ;
        case EMSGSIZE:     return WSAEMSGSIZE;
        case EPROTOTYPE:   return WSAEPROTOTYPE;
        case ENOPROTOOPT:  return WSAENOPROTOOPT;
        case EPROTONOSUPPORT: return WSAEPROTONOSUPPORT;
        case EOPNOTSUPP:   return WSAEOPNOTSUPP;
        case EAFNOSUPPORT: return WSAEAFNOSUPPORT;
        case EADDRINUSE:   return WSAEADDRINUSE;
        case EADDRNOTAVAIL: return WSAEADDRNOTAVAIL;
        case ENETDOWN:     return WSAENETDOWN;
        case ENETUNREACH:  return WSAENETUNREACH;
        case ENETRESET:    return WSAENETRESET;
        case ECONNABORTED: return WSAECONNABORTED;
        case ECONNRESET:   return WSAECONNRESET;
        case EPIPE:        return WSAECONNRESET;
        case ENOBUFS:      return WSAENOBUFS;
        case EISCONN:      return WSAEISCONN;
        case ENOTCONN:     return WSAENOTCONN;
        case ESHUTDOWN:    return WSAESHUTDOWN;
        case ETIMEDOUT:    return WSAETIMEDOUT;
        case ECONNREFUSED: return WSAECONNREFUSED;
        case EHOSTDOWN:    return WSAEHOSTDOWN;
        case EHOSTUNREACH: return WSAEHOSTUNREACH;
        default:           return WSABASEERR + e;
        }
    }

    // Background watcher translating socket readiness into Winsock network events.
    class SocketWatcher {
    public:
        static SocketWatcher& Get() { static SocketWatcher w; return w; }

        void Select(int fd, HANDLE ev, long mask)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!ev || mask == 0) { m_regs.erase(fd); Wake(); return; }
            Reg& r = m_regs[fd];
            r.event = ev;
            r.mask = mask;
            r.pending = 0;
            r.closed = false;
            sockaddr_storage peer;
            socklen_t len = sizeof(peer);
            r.connected = ::getpeername(fd, (sockaddr*)&peer, &len) == 0;
            r.writable = false;
            if (r.connected && (mask & FD_WRITE)) {   // Windows posts FD_WRITE for a ready, connected socket
                r.writable = true;
                Post(r, FD_WRITE_BIT, 0);
            }
            Wake();
        }

        void Remove(int fd)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_regs.erase(fd);
            Wake();
        }

        bool Enum(int fd, HANDLE ev, WSANETWORKEVENTS* out)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_regs.find(fd);
            std::memset(out, 0, sizeof(*out));
            if (it != m_regs.end()) {
                Reg& r = it->second;
                out->lNetworkEvents = r.pending;
                std::memcpy(out->iErrorCode, r.errors, sizeof(r.errors));
                r.pending = 0;
                std::memset(r.errors, 0, sizeof(r.errors));
            }
            if (ev) {
                std::lock_guard<std::mutex> klock(KernelLock());
                if (auto* e = dynamic_cast<KEvent*>(Obj(ev))) e->signaled = false;
            }
            Wake();
            return it != m_regs.end();
        }

    private:
        struct Reg {
            HANDLE event = nullptr;
            long mask = 0;
            long pending = 0;
            int errors[FD_MAX_EVENTS] = {};
            bool connected = false;
            bool writable = false;
            bool closed = false;
        };

        SocketWatcher()
        {
            if (::pipe(m_wake) == 0) {
                ::fcntl(m_wake[0], F_SETFL, O_NONBLOCK);
                ::fcntl(m_wake[1], F_SETFL, O_NONBLOCK);
            }
            std::thread([this] { Run(); }).detach();
        }

        void Wake() { const char c = 0; (void)::write(m_wake[1], &c, 1); }

        // Called with m_mutex held.
        void Post(Reg& r, int bit, int error)
        {
            r.pending |= (1L << bit);
            r.errors[bit] = error;
            std::lock_guard<std::mutex> klock(KernelLock());
            if (auto* e = dynamic_cast<KEvent*>(Obj(r.event))) e->signaled = true;
            Signal();
        }

        void Run()
        {
            for (;;) {
                std::vector<pollfd> fds;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    fds.push_back({m_wake[0], POLLIN, 0});
                    for (auto& [fd, r] : m_regs) {
                        short events = 0;
                        if (!r.connected && (r.mask & FD_CONNECT)) events |= POLLOUT;
                        if (r.connected && !r.closed && (r.mask & (FD_READ | FD_CLOSE)) &&
                            !(r.pending & (FD_READ | FD_CLOSE)))
                            events |= POLLIN;
                        if (r.connected && !r.closed && (r.mask & FD_WRITE) && !r.writable)
                            events |= POLLOUT;
                        if (events) fds.push_back({fd, events, 0});
                    }
                }
                ::poll(fds.data(), (nfds_t)fds.size(), 100);
                if (fds[0].revents & POLLIN) { char buf[64]; while (::read(m_wake[0], buf, sizeof(buf)) > 0) {} }

                std::lock_guard<std::mutex> lock(m_mutex);
                for (size_t i = 1; i < fds.size(); ++i) {
                    auto it = m_regs.find(fds[i].fd);
                    if (it == m_regs.end() || !fds[i].revents) continue;
                    Reg& r = it->second;
                    const short rev = fds[i].revents;
                    if (!r.connected && (r.mask & FD_CONNECT)) {
                        int err = 0;
                        socklen_t len = sizeof(err);
                        ::getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &err, &len);
                        if (err == 0) {
                            // A connect that failed synchronously (common on localhost) already
                            // consumed SO_ERROR; only a real peer means success.
                            sockaddr_storage peer;
                            socklen_t plen = sizeof(peer);
                            if (::getpeername(fds[i].fd, (sockaddr*)&peer, &plen) != 0) err = ECONNREFUSED;
                        }
                        Post(r, FD_CONNECT_BIT, ErrnoToWsa(err));
                        r.connected = (err == 0);
                        if (err != 0) r.closed = true;
                        else if (r.mask & FD_WRITE) { r.writable = true; Post(r, FD_WRITE_BIT, 0); }
                        continue;
                    }
                    if (rev & (POLLIN | POLLHUP | POLLERR)) {
                        char peek;
                        const ssize_t n = ::recv(fds[i].fd, &peek, 1, MSG_PEEK | MSG_DONTWAIT);
                        if (n > 0) {
                            if (r.mask & FD_READ) Post(r, FD_READ_BIT, 0);
                        } else if (n == 0 || (errno != EWOULDBLOCK && errno != EINTR)) {
                            r.closed = true;
                            if (r.mask & FD_CLOSE) Post(r, FD_CLOSE_BIT, n == 0 ? 0 : ErrnoToWsa(errno));
                        }
                    }
                    if ((rev & POLLOUT) && r.connected && !r.closed && (r.mask & FD_WRITE) && !r.writable) {
                        r.writable = true;
                        Post(r, FD_WRITE_BIT, 0);
                    }
                }
                // Re-check fullness: a socket stays "writable" until its send buffer fills up.
                for (auto& [fd, r] : m_regs) {
                    if (!r.writable || r.closed) continue;
                    pollfd p = {fd, POLLOUT, 0};
                    if (::poll(&p, 1, 0) == 0) r.writable = false;   // full now; next POLLOUT edge posts FD_WRITE
                }
            }
        }

        std::mutex m_mutex;
        std::map<int, Reg> m_regs;
        int m_wake[2] = {-1, -1};
    };
}

inline int WSAStartup(WORD version, LPWSADATA data)
{
    if (data) {
        std::memset(data, 0, sizeof(*data));
        data->wVersion = version;
        data->wHighVersion = MAKEWORD(2, 2);
        std::strcpy(data->szDescription, "RAN macOS BSD sockets");
        std::strcpy(data->szSystemStatus, "Running");
    }
    ::signal(SIGPIPE, SIG_IGN);   // Windows send() never raises a signal on a dead peer
    return 0;
}
inline int WSACleanup() { return 0; }
inline int WSAGetLastError() { return ran_compat::ErrnoToWsa(errno); }
inline void WSASetLastError(int e) { errno = e >= WSABASEERR ? e - WSABASEERR : e; }

inline int closesocket(SOCKET s)
{
    ran_compat::SocketWatcher::Get().Remove((int)s);
    return ::close((int)s) == 0 ? 0 : SOCKET_ERROR;
}

inline int ioctlsocket(SOCKET s, long cmd, u_long* arg)
{
    if (cmd == (long)FIONBIO) {
        const int flags = ::fcntl((int)s, F_GETFL, 0);
        return ::fcntl((int)s, F_SETFL, (arg && *arg) ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) == -1 ? SOCKET_ERROR : 0;
    }
    if (cmd == (long)FIONREAD) {
        int n = 0;
        if (::ioctl((int)s, FIONREAD, &n) == -1) return SOCKET_ERROR;
        if (arg) *arg = (u_long)n;
        return 0;
    }
    errno = EINVAL;
    return SOCKET_ERROR;
}

inline WSAEVENT WSACreateEvent() { return CreateEvent(nullptr, TRUE, FALSE, nullptr); }
inline BOOL WSACloseEvent(WSAEVENT ev) { return CloseHandle(ev); }
inline BOOL WSASetEvent(WSAEVENT ev) { return SetEvent(ev); }
inline BOOL WSAResetEvent(WSAEVENT ev) { return ResetEvent(ev); }
inline DWORD WSAWaitForMultipleEvents(DWORD n, const WSAEVENT* evs, BOOL waitAll, DWORD ms, BOOL)
{
    return WaitForMultipleObjects(n, evs, waitAll, ms);
}

inline int WSAEventSelect(SOCKET s, WSAEVENT ev, long mask)
{
    // Like Windows, WSAEventSelect puts the socket into non-blocking mode.
    const int flags = ::fcntl((int)s, F_GETFL, 0);
    if (flags == -1) return SOCKET_ERROR;
    ::fcntl((int)s, F_SETFL, flags | O_NONBLOCK);
    ran_compat::SocketWatcher::Get().Select((int)s, ev, mask);
    return 0;
}

inline int WSAEnumNetworkEvents(SOCKET s, WSAEVENT ev, LPWSANETWORKEVENTS out)
{
    if (!out) { errno = EFAULT; return SOCKET_ERROR; }
    if (!ran_compat::SocketWatcher::Get().Enum((int)s, ev, out)) { errno = ENOTSOCK; return SOCKET_ERROR; }
    return 0;
}

// Overlapped I/O and service lookup are not used by the client's network path.
inline int WSARecv(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, LPWSAOVERLAPPED, void*) { errno = EOPNOTSUPP; return SOCKET_ERROR; }
inline int WSARecvFrom(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, sockaddr*, int*, LPWSAOVERLAPPED, void*) { errno = EOPNOTSUPP; return SOCKET_ERROR; }
inline int WSASend(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED, void*) { errno = EOPNOTSUPP; return SOCKET_ERROR; }
inline int WSALookupServiceNext(HANDLE, DWORD, LPDWORD, void*) { errno = EOPNOTSUPP; return SOCKET_ERROR; }
inline int WSALookupServiceEnd(HANDLE) { return 0; }

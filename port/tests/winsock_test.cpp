// Behaviour tests for the Winsock stand-in against a real local TCP server: non-blocking connect
// with FD_CONNECT, FD_WRITE after connect, FD_READ on data, FD_CLOSE on peer close, and a refused
// connect reporting WSAECONNREFUSED - the exact event flow the game's NetClient relies on.
#include <winsock2.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static int ListenOnLoopback(unsigned short& port)
{
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    ::bind(s, (sockaddr*)&a, sizeof(a));
    ::listen(s, 4);
    socklen_t len = sizeof(a);
    ::getsockname(s, (sockaddr*)&a, &len);
    port = ntohs(a.sin_port);
    return s;
}

static long WaitEvents(SOCKET s, WSAEVENT ev, WSANETWORKEVENTS& ne, DWORD ms = 3000)
{
    if (WSAWaitForMultipleEvents(1, &ev, FALSE, ms, FALSE) != WSA_WAIT_EVENT_0) return 0;
    if (WSAEnumNetworkEvents(s, ev, &ne) == SOCKET_ERROR) return -1;
    return ne.lNetworkEvents;
}

int main()
{
    WSADATA wsa;
    CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

    unsigned short port = 0;
    const int listener = ListenOnLoopback(port);
    std::atomic<bool> serverDone{false};
    std::thread server([&] {
        const int c = ::accept(listener, nullptr, nullptr);
        char buf[16];
        const ssize_t n = ::recv(c, buf, sizeof(buf), 0);   // echo one message
        if (n > 0) ::send(c, buf, (size_t)n, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        ::close(c);                                          // then close -> FD_CLOSE
        serverDone = true;
    });

    // Non-blocking connect, the way s_NetClient.cpp does it.
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(s != INVALID_SOCKET);
    u_long nonBlk = 1;
    CHECK(ioctlsocket(s, FIONBIO, &nonBlk) != SOCKET_ERROR);
    WSAEVENT ev = WSACreateEvent();
    CHECK(WSAEventSelect(s, ev, FD_CONNECT) != SOCKET_ERROR);
    SOCKADDR_IN addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    const int rc = ::connect(s, (SOCKADDR*)&addr, sizeof(addr));
    CHECK(rc == 0 || WSAGetLastError() == WSAEWOULDBLOCK);
    WSANETWORKEVENTS ne;
    CHECK(WaitEvents(s, ev, ne) & FD_CONNECT);
    CHECK(ne.iErrorCode[FD_CONNECT_BIT] == 0);
    WSACloseEvent(ev);

    // Steady state: FD_WRITE right after selecting on a connected socket, then FD_READ, FD_CLOSE.
    WSAEVENT ev2 = WSACreateEvent();
    CHECK(WSAEventSelect(s, ev2, FD_READ | FD_WRITE | FD_CLOSE) != SOCKET_ERROR);
    CHECK(WaitEvents(s, ev2, ne) & FD_WRITE);
    CHECK(::send(s, "ping", 4, 0) == 4);
    CHECK(WaitEvents(s, ev2, ne) & FD_READ);
    char in[16] = {};
    CHECK(::recv(s, in, sizeof(in), 0) == 4 && std::strncmp(in, "ping", 4) == 0);
    long closeEv = 0;
    for (int i = 0; i < 5 && !(closeEv & FD_CLOSE); ++i) closeEv = WaitEvents(s, ev2, ne);
    CHECK(closeEv & FD_CLOSE);
    // After FD_CLOSE the event stays quiet (Windows posts FD_CLOSE once).
    CHECK(WSAWaitForMultipleEvents(1, &ev2, FALSE, 300, FALSE) == WSA_WAIT_TIMEOUT);
    CHECK(closesocket(s) == 0);
    WSACloseEvent(ev2);
    server.join();
    ::close(listener);

    // Refused connect reports the error through FD_CONNECT, like Windows.
    unsigned short deadPort = 0;
    const int tmp = ListenOnLoopback(deadPort);
    ::close(tmp);   // nothing listens on deadPort now
    SOCKET s2 = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ioctlsocket(s2, FIONBIO, &nonBlk);
    WSAEVENT ev3 = WSACreateEvent();
    WSAEventSelect(s2, ev3, FD_CONNECT);
    addr.sin_port = htons(deadPort);
    ::connect(s2, (SOCKADDR*)&addr, sizeof(addr));
    CHECK(WaitEvents(s2, ev3, ne) & FD_CONNECT);
    CHECK(ne.iErrorCode[FD_CONNECT_BIT] == WSAECONNREFUSED);
    closesocket(s2);
    WSACloseEvent(ev3);

    // Error code mapping.
    errno = EWOULDBLOCK;
    CHECK(WSAGetLastError() == WSAEWOULDBLOCK);
    errno = ECONNRESET;
    CHECK(WSAGetLastError() == WSAECONNRESET);

    CHECK(WSACleanup() == 0);
    if (g_failed == 0) std::printf("PASS winsock_test\n");
    return g_failed == 0 ? 0 : 1;
}

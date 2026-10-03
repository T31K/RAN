// MFC WinInet classes (CInternetSession / CHttpConnection / CHttpFile) for the native macOS build.
// Only the old in-game HTTP patcher (NetClient s_CHttpPatch) uses them; the native app updates
// through the launcher's OTA instead. Every request fails the MFC way - by throwing a
// CInternetException* - which the patcher already catches and reports.
#pragma once
#include "mfc/afx_window.h"

#define INTERNET_FLAG_RELOAD            0x80000000
#define INTERNET_FLAG_DONT_CACHE        0x04000000
#define INTERNET_FLAG_NO_CACHE_WRITE    0x04000000
#define INTERNET_FLAG_TRANSFER_BINARY   0x00000002
#define INTERNET_FLAG_EXISTING_CONNECT  0x20000000
#define INTERNET_DEFAULT_HTTP_PORT      80
#define HTTP_QUERY_STATUS_CODE          19
#define HTTP_QUERY_RAW_HEADERS_CRLF     22
#define ERROR_INTERNET_CANNOT_CONNECT   12029
#define AFX_INET_SERVICE_FTP            1
#define AFX_INET_SERVICE_HTTP           3
#define AFX_INET_SERVICE_HTTPS          4097
typedef WORD INTERNET_PORT;

// WinInet HTTP status codes.
#define HTTP_STATUS_CONTINUE            100
#define HTTP_STATUS_SWITCH_PROTOCOLS    101
#define HTTP_STATUS_OK                  200
#define HTTP_STATUS_CREATED             201
#define HTTP_STATUS_ACCEPTED            202
#define HTTP_STATUS_PARTIAL             203
#define HTTP_STATUS_NO_CONTENT          204
#define HTTP_STATUS_RESET_CONTENT       205
#define HTTP_STATUS_PARTIAL_CONTENT     206
#define HTTP_STATUS_AMBIGUOUS           300
#define HTTP_STATUS_MOVED               301
#define HTTP_STATUS_REDIRECT            302
#define HTTP_STATUS_REDIRECT_METHOD     303
#define HTTP_STATUS_NOT_MODIFIED        304
#define HTTP_STATUS_USE_PROXY           305
#define HTTP_STATUS_REDIRECT_KEEP_VERB  307
#define HTTP_STATUS_BAD_REQUEST         400
#define HTTP_STATUS_DENIED              401
#define HTTP_STATUS_PAYMENT_REQ         402
#define HTTP_STATUS_FORBIDDEN           403
#define HTTP_STATUS_NOT_FOUND           404
#define HTTP_STATUS_BAD_METHOD          405
#define HTTP_STATUS_NONE_ACCEPTABLE     406
#define HTTP_STATUS_PROXY_AUTH_REQ      407
#define HTTP_STATUS_REQUEST_TIMEOUT     408
#define HTTP_STATUS_CONFLICT            409
#define HTTP_STATUS_GONE                410
#define HTTP_STATUS_LENGTH_REQUIRED     411
#define HTTP_STATUS_PRECOND_FAILED      412
#define HTTP_STATUS_REQUEST_TOO_LARGE   413
#define HTTP_STATUS_URI_TOO_LONG        414
#define HTTP_STATUS_UNSUPPORTED_MEDIA   415
#define HTTP_STATUS_RETRY_WITH          449
#define HTTP_STATUS_SERVER_ERROR        500
#define HTTP_STATUS_NOT_SUPPORTED       501
#define HTTP_STATUS_BAD_GATEWAY         502
#define HTTP_STATUS_SERVICE_UNAVAIL     503
#define HTTP_STATUS_GATEWAY_TIMEOUT     504
#define HTTP_STATUS_VERSION_NOT_SUP     505

class CException : public CObject
{
public:
    virtual BOOL GetErrorMessage(char* buf, UINT max, UINT* = nullptr) const
    {
        if (buf && max) buf[0] = 0;
        return FALSE;
    }
    void Delete() { delete this; }
};

class CInternetException : public CException
{
public:
    explicit CInternetException(DWORD error) : m_dwError(error) {}
    BOOL GetErrorMessage(char* buf, UINT max, UINT* = nullptr) const override
    {
        if (buf && max) std::snprintf(buf, max, "Internet error %u (HTTP patching is not available in the native build)", (unsigned)m_dwError);
        return TRUE;
    }
    DWORD m_dwError;
};

class CHttpFile : public CObject
{
public:
    BOOL AddRequestHeaders(const char*, DWORD = 0, int = -1) { return TRUE; }
    BOOL SendRequest(const char* = nullptr, DWORD = 0, void* = nullptr, DWORD = 0) { throw new CInternetException(ERROR_INTERNET_CANNOT_CONNECT); }
    BOOL SendRequest(const char*, void*, DWORD) { throw new CInternetException(ERROR_INTERNET_CANNOT_CONNECT); }
    BOOL QueryInfoStatusCode(DWORD& code) const { code = 0; return FALSE; }
    BOOL QueryInfo(DWORD, CString& out, DWORD* = nullptr) const { out.Empty(); return FALSE; }
    UINT Read(void*, UINT) { return 0; }
    void Close() {}
};

class CHttpConnection : public CObject
{
public:
    enum { HTTP_VERB_POST = 0, HTTP_VERB_GET = 1, HTTP_VERB_HEAD = 2 };
    CHttpFile* OpenRequest(int, const char*, const char* = nullptr, DWORD_PTR = 1,
                           const char** = nullptr, const char* = nullptr, DWORD = 0)
    {
        return new CHttpFile();
    }
    void Close() {}
};

// Splits "http://host[:port]/path" like MFC's AfxParseURL.
inline BOOL AfxParseURL(const char* url, DWORD& serviceType, CString& server, CString& object, INTERNET_PORT& port)
{
    serviceType = 0;
    server.Empty();
    object.Empty();
    port = INTERNET_DEFAULT_HTTP_PORT;
    if (!url) return FALSE;
    const char* p = url;
    if (strncasecmp(p, "http://", 7) == 0) { serviceType = AFX_INET_SERVICE_HTTP; p += 7; }
    else if (strncasecmp(p, "https://", 8) == 0) { serviceType = AFX_INET_SERVICE_HTTPS; port = 443; p += 8; }
    else return FALSE;
    const char* slash = std::strchr(p, '/');
    const char* hostEnd = slash ? slash : p + std::strlen(p);
    const char* colon = (const char*)std::memchr(p, ':', (size_t)(hostEnd - p));
    server = CString(p, (int)((colon ? colon : hostEnd) - p));
    if (colon) port = (INTERNET_PORT)std::atoi(colon + 1);
    object = slash ? CString(slash) : CString("/");
    return !server.IsEmpty();
}

class CInternetSession : public CObject
{
public:
    explicit CInternetSession(const char* = nullptr, DWORD_PTR = 1, DWORD = 0, const char* = nullptr,
                              const char* = nullptr, DWORD = 0) {}
    CHttpConnection* GetHttpConnection(const char*, DWORD = 0, int = INTERNET_DEFAULT_HTTP_PORT,
                                       const char* = nullptr, const char* = nullptr)
    {
        return new CHttpConnection();
    }
    CHttpConnection* GetHttpConnection(const char* server, int port) { return GetHttpConnection(server, 0, port); }
    CObject* OpenURL(const char*, DWORD_PTR = 1, DWORD = 0, const char* = nullptr, DWORD = 0)
    {
        throw new CInternetException(ERROR_INTERNET_CANNOT_CONNECT);
    }
    void Close() {}
};

// See telemetry_mac.h.
#include "telemetry_mac.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <curl/curl.h>
#include <cstdlib>
#include <dlfcn.h>
#include <sys/sysctl.h>

// The Objective-C runtime, declared by hand: <objc/objc.h> defines BOOL, which the game's
// Windows types also define.
extern "C" void* objc_getClass(const char*);
extern "C" void* sel_registerName(const char*);
extern "C" void objc_msgSend(void);

namespace ran_telemetry_mac {

std::string Sysctl(const char* name)
{
    char buf[512];
    size_t len = sizeof(buf);
    if (sysctlbyname(name, buf, &len, nullptr, 0) != 0 || len == 0) return "";
    return std::string(buf, strnlen(buf, len));
}

long long SysctlInt(const char* name)
{
    long long v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, nullptr, 0) != 0) return 0;
    return v;
}

std::string BundleValue(const char* infoPlistKey)
{
    CFBundleRef b = CFBundleGetMainBundle();
    if (!b) return "";
    CFStringRef key = CFStringCreateWithCString(nullptr, infoPlistKey, kCFStringEncodingUTF8);
    CFTypeRef v = CFBundleGetValueForInfoDictionaryKey(b, key);
    CFRelease(key);
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return "";
    char buf[1024];
    if (!CFStringGetCString((CFStringRef)v, buf, sizeof(buf), kCFStringEncodingUTF8)) return "";
    return buf;
}

std::string GpuName()
{
    void* metal = dlopen("/System/Library/Frameworks/Metal.framework/Metal", RTLD_LAZY);
    if (!metal) return "";
    auto create = (void* (*)())dlsym(metal, "MTLCreateSystemDefaultDevice");
    void* dev = create ? create() : nullptr;
    if (!dev) return "";
    auto send = (void* (*)(void*, void*))objc_msgSend;
    void* name = send(dev, sel_registerName("name"));
    auto utf8 = (const char* (*)(void*, void*))objc_msgSend;
    const char* s = name ? utf8(name, sel_registerName("UTF8String")) : nullptr;
    std::string out = s ? s : "";
    send(dev, sel_registerName("release"));
    return out;
}

std::string SupportDir()
{
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/Library/Application Support/RanOdyssey Native";
}

std::string EncodePng(const std::vector<unsigned char>& bgra, unsigned w, unsigned h, unsigned maxWidth)
{
    if (!w || !h || bgra.size() < (size_t)w * h * 4) return "";
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef src = CGBitmapContextCreate((void*)bgra.data(), w, h, 8, (size_t)w * 4, cs,
                                             kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = src ? CGBitmapContextCreateImage(src) : nullptr;
    if (img && w > maxWidth) {   // keep reports small: scale down wide screenshots
        const unsigned nw = maxWidth, nh = (unsigned)((double)h * maxWidth / w);
        CGContextRef dst = CGBitmapContextCreate(nullptr, nw, nh, 8, 0, cs, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
        if (dst) {
            CGContextSetInterpolationQuality(dst, kCGInterpolationHigh);
            CGContextDrawImage(dst, CGRectMake(0, 0, nw, nh), img);
            CGImageRef small = CGBitmapContextCreateImage(dst);
            CGContextRelease(dst);
            if (small) { CGImageRelease(img); img = small; }
        }
    }
    std::string out;
    if (img) {
        CFMutableDataRef data = CFDataCreateMutable(nullptr, 0);
        CGImageDestinationRef d = CGImageDestinationCreateWithData(data, CFSTR("public.png"), 1, nullptr);
        if (d) {
            CGImageDestinationAddImage(d, img, nullptr);
            if (CGImageDestinationFinalize(d)) out.assign((const char*)CFDataGetBytePtr(data), (size_t)CFDataGetLength(data));
            CFRelease(d);
        }
        CFRelease(data);
        CGImageRelease(img);
    }
    if (src) CGContextRelease(src);
    CGColorSpaceRelease(cs);
    return out;
}

namespace {
size_t Discard(char*, size_t size, size_t n, void*) { return size * n; }
}

long HttpPostJson(const std::string& url, const std::string& ingestKey, const std::string& body)
{
    static const bool init = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!init) return 0;
    CURL* c = curl_easy_init();
    if (!c) return 0;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, ("X-Ran-Key: " + ingestKey).c_str());
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size());
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, Discard);
    long status = 0;
    if (curl_easy_perform(c) == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return status;
}

} // namespace ran_telemetry_mac

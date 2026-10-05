// macOS services for the error reports (port/platform/telemetry.cpp), kept free of the Windows
// and D3D headers: system facts, PNG encoding, the HTTPS upload.
#pragma once
#include <string>
#include <vector>

namespace ran_telemetry_mac {

std::string Sysctl(const char* name);                 // "" if unavailable
long long SysctlInt(const char* name);                // 0 if unavailable
std::string BundleValue(const char* infoPlistKey);    // main bundle Info.plist string, "" if none
std::string GpuName();                                 // Metal default device name
std::string SupportDir();                              // ~/Library/Application Support/RanOdyssey Native

// BGRA rows (top first) -> PNG bytes; scaled down to at most maxWidth wide. "" on failure.
std::string EncodePng(const std::vector<unsigned char>& bgra, unsigned w, unsigned h, unsigned maxWidth);

// POST a JSON body; returns the HTTP status (0 = no connection).
long HttpPostJson(const std::string& url, const std::string& ingestKey, const std::string& body);

} // namespace ran_telemetry_mac

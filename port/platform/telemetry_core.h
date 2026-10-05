// Error reports (native build): the pure parts of port/platform/telemetry.cpp - report JSON,
// base64 for the screenshot, and deciding which logged game errors are worth a report.
#pragma once
#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ran_telemetry {

// A JSON string literal. Game text is CP949, not UTF-8: bytes >= 0x80 are written as \u00XX so
// the report stays valid JSON (the byte values survive for decoding later).
inline std::string JsonString(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20 || c >= 0x80) {
                char b[8];
                std::snprintf(b, sizeof(b), "\\u%04x", c);
                o += b;
            } else {
                o += char(c);
            }
        }
    }
    return o + "\"";
}

// A flat JSON object with fields in insertion order; Set on an existing key replaces its value.
class Report {
public:
    void Set(const std::string& key, const std::string& value) { Put(key, JsonString(value)); }
    void SetNumber(const std::string& key, double v)
    {
        char b[32];
        std::snprintf(b, sizeof(b), "%.15g", v);
        Put(key, b);
    }
    std::string Json() const
    {
        std::string o = "{";
        for (size_t i = 0; i < m_fields.size(); ++i)
            o += (i ? "," : "") + JsonString(m_fields[i].first) + ":" + m_fields[i].second;
        return o + "}";
    }

private:
    void Put(const std::string& key, const std::string& raw)
    {
        for (auto& f : m_fields)
            if (f.first == key) { f.second = raw; return; }
        m_fields.emplace_back(key, raw);
    }
    std::vector<std::pair<std::string, std::string>> m_fields;
};

inline std::string Base64(const std::string& in)
{
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const unsigned v = (unsigned char)in[i] << 16;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == in.size()) {
        const unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
    }
    return o;
}

inline std::string Lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// A game log line worth a report: it mentions an error or a failure. The "ERROR REPORT" banner
// CDebugSet writes at the top of each log is not one.
inline bool IsErrorLine(const std::string& line)
{
    const std::string l = Lower(line);
    if (l.find("error report") != std::string::npos) return false;
    return l.find("error") != std::string::npos || l.find("fail") != std::string::npos;
}

// The line without digits: the same error with another time, id or count is the same error.
inline std::string Normalize(const std::string& line)
{
    std::string o;
    for (char c : line)
        if (!std::isdigit((unsigned char)c)) o += c;
    return o;
}

// At most one report per `interval` seconds and `cap` per session; each distinct line once.
class ErrorThrottle {
public:
    ErrorThrottle(double interval, int cap) : m_interval(interval), m_cap(cap) {}
    bool Allow(const std::string& line, double now)
    {
        if (m_sent >= m_cap) return false;
        if (m_sent > 0 && now - m_last < m_interval) return false;
        if (!m_seen.insert(Normalize(line)).second) return false;
        m_last = now;
        ++m_sent;
        return true;
    }

private:
    double m_interval;
    int m_cap;
    int m_sent = 0;
    double m_last = 0;
    std::set<std::string> m_seen;
};

} // namespace ran_telemetry

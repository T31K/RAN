// MFC CTime/CTimeSpan/CPoint/CSize/CRect stand-ins for the native macOS build.
// Expects the Win32 base types (POINT/RECT/SIZE/LONG/LPRECT) from DXVK's native <windows.h>.
#pragma once
#include <cstdint>
#include <ctime>
#include "mfc/afx_string.h"

#ifndef _TIME64_T_DEFINED
#define _TIME64_T_DEFINED
typedef int64_t __time64_t;
#endif

class CTimeSpan
{
public:
    CTimeSpan() = default;
    explicit CTimeSpan(__time64_t seconds) : m_span(seconds) {}
    CTimeSpan(LONG days, int hours, int mins, int secs)
        : m_span(((( (__time64_t)days * 24 + hours) * 60) + mins) * 60 + secs) {}

    __time64_t GetTimeSpan() const { return m_span; }
    LONGLONG GetDays() const { return m_span / 86400; }
    LONG GetHours() const { return (LONG)((m_span / 3600) % 24); }
    LONG GetMinutes() const { return (LONG)((m_span / 60) % 60); }
    LONG GetSeconds() const { return (LONG)(m_span % 60); }
    LONGLONG GetTotalHours() const { return m_span / 3600; }
    LONGLONG GetTotalMinutes() const { return m_span / 60; }
    LONGLONG GetTotalSeconds() const { return m_span; }

    CTimeSpan operator+(CTimeSpan o) const { return CTimeSpan(m_span + o.m_span); }
    CTimeSpan operator-(CTimeSpan o) const { return CTimeSpan(m_span - o.m_span); }
    bool operator==(CTimeSpan o) const { return m_span == o.m_span; }
    bool operator!=(CTimeSpan o) const { return m_span != o.m_span; }
    bool operator<(CTimeSpan o) const { return m_span < o.m_span; }
    bool operator>(CTimeSpan o) const { return m_span > o.m_span; }
    bool operator<=(CTimeSpan o) const { return m_span <= o.m_span; }
    bool operator>=(CTimeSpan o) const { return m_span >= o.m_span; }

private:
    __time64_t m_span = 0;
};

// Local-time calendar helpers, as MFC's CTime uses the C runtime's local time.
class CTime
{
public:
    CTime() = default;
    CTime(__time64_t t) : m_time(t) {}
    CTime(int year, int month, int day, int hour, int min, int sec, int dst = -1)
    {
        std::tm tmv = {};
        tmv.tm_year = year - 1900;
        tmv.tm_mon = month - 1;
        tmv.tm_mday = day;
        tmv.tm_hour = hour;
        tmv.tm_min = min;
        tmv.tm_sec = sec;
        tmv.tm_isdst = dst;
        m_time = (__time64_t)std::mktime(&tmv);
    }

    static CTime GetCurrentTime() { return CTime((__time64_t)std::time(nullptr)); }
    __time64_t GetTime() const { return m_time; }

    int GetYear() const { return Local().tm_year + 1900; }
    int GetMonth() const { return Local().tm_mon + 1; }
    int GetDay() const { return Local().tm_mday; }
    int GetHour() const { return Local().tm_hour; }
    int GetMinute() const { return Local().tm_min; }
    int GetSecond() const { return Local().tm_sec; }
    int GetDayOfWeek() const { return Local().tm_wday + 1; }   // 1 = Sunday

    CString Format(const char* fmt) const
    {
        std::tm tmv = Local();
        char buf[256];
        const size_t n = std::strftime(buf, sizeof(buf), fmt, &tmv);
        return CString(buf, (int)n);
    }

    CTime operator+(CTimeSpan s) const { return CTime(m_time + s.GetTimeSpan()); }
    CTime operator-(CTimeSpan s) const { return CTime(m_time - s.GetTimeSpan()); }
    CTimeSpan operator-(CTime o) const { return CTimeSpan(m_time - o.m_time); }
    CTime& operator+=(CTimeSpan s) { m_time += s.GetTimeSpan(); return *this; }
    CTime& operator-=(CTimeSpan s) { m_time -= s.GetTimeSpan(); return *this; }
    bool operator==(CTime o) const { return m_time == o.m_time; }
    bool operator!=(CTime o) const { return m_time != o.m_time; }
    bool operator<(CTime o) const { return m_time < o.m_time; }
    bool operator>(CTime o) const { return m_time > o.m_time; }
    bool operator<=(CTime o) const { return m_time <= o.m_time; }
    bool operator>=(CTime o) const { return m_time >= o.m_time; }

private:
    std::tm Local() const
    {
        std::tm tmv = {};
        const time_t t = (time_t)m_time;
        localtime_r(&t, &tmv);
        return tmv;
    }

    __time64_t m_time = 0;
};

class CPoint : public POINT
{
public:
    CPoint() { x = 0; y = 0; }
    CPoint(int ix, int iy) { x = ix; y = iy; }
    CPoint(POINT p) { x = p.x; y = p.y; }
    CPoint& operator+=(POINT p) { x += p.x; y += p.y; return *this; }
    CPoint& operator-=(POINT p) { x -= p.x; y -= p.y; return *this; }
    CPoint operator+(POINT p) const { return CPoint(x + p.x, y + p.y); }
    CPoint operator-(POINT p) const { return CPoint(x - p.x, y - p.y); }
    bool operator==(POINT p) const { return x == p.x && y == p.y; }
    bool operator!=(POINT p) const { return !(*this == p); }
};

class CSize : public SIZE
{
public:
    CSize() { cx = 0; cy = 0; }
    CSize(int w, int h) { cx = w; cy = h; }
    CSize(SIZE s) { cx = s.cx; cy = s.cy; }
    bool operator==(SIZE s) const { return cx == s.cx && cy == s.cy; }
    bool operator!=(SIZE s) const { return !(*this == s); }
};

class CRect : public RECT
{
public:
    CRect() { left = top = right = bottom = 0; }
    CRect(int l, int t, int r, int b) { left = l; top = t; right = r; bottom = b; }
    CRect(const RECT& r) { left = r.left; top = r.top; right = r.right; bottom = r.bottom; }
    CRect(POINT topLeft, SIZE size)
    {
        left = topLeft.x; top = topLeft.y; right = left + size.cx; bottom = top + size.cy;
    }

    operator LPRECT() { return this; }
    operator const RECT*() const { return this; }

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    CSize Size() const { return CSize(Width(), Height()); }
    CPoint TopLeft() const { return CPoint(left, top); }
    CPoint BottomRight() const { return CPoint(right, bottom); }
    CPoint CenterPoint() const { return CPoint((left + right) / 2, (top + bottom) / 2); }
    bool IsRectEmpty() const { return Width() <= 0 || Height() <= 0; }
    bool PtInRect(POINT p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }

    void SetRect(int l, int t, int r, int b) { left = l; top = t; right = r; bottom = b; }
    void OffsetRect(int dx, int dy) { left += dx; right += dx; top += dy; bottom += dy; }
    void OffsetRect(POINT p) { OffsetRect(p.x, p.y); }
    void InflateRect(int dx, int dy) { left -= dx; right += dx; top -= dy; bottom += dy; }
    void DeflateRect(int dx, int dy) { InflateRect(-dx, -dy); }
    void NormalizeRect()
    {
        if (left > right) { LONG t = left; left = right; right = t; }
        if (top > bottom) { LONG t = top; top = bottom; bottom = t; }
    }
    bool operator==(const RECT& r) const { return left == r.left && top == r.top && right == r.right && bottom == r.bottom; }
    bool operator!=(const RECT& r) const { return !(*this == r); }
};

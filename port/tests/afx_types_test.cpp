// Behaviour tests for the MFC CTime/CTimeSpan/CPoint/CSize/CRect stand-ins.
#include <windows.h>        // DXVK native: POINT/RECT/SIZE/LONG
#include "mfc/afx_types.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

int main()
{
    setenv("TZ", "UTC", 1);
    tzset();

    // CTime from components and back (local time; TZ=UTC for determinism).
    CTime t(2026, 10, 3, 21, 30, 15);
    CHECK(t.GetYear() == 2026 && t.GetMonth() == 10 && t.GetDay() == 3);
    CHECK(t.GetHour() == 21 && t.GetMinute() == 30 && t.GetSecond() == 15);
    CHECK(t.GetDayOfWeek() == 7);                       // 2026-10-03 is a Saturday; MFC: 1 = Sunday
    CHECK(t.GetTime() == (__time64_t)1791063015);       // calendar.timegm((2026,10,3,21,30,15))
    CHECK(sizeof(t.GetTime()) == 8);

    // Round trip through the stored __time64_t (how item expiry times are kept).
    __time64_t raw = t.GetTime();
    CTime t2(raw);
    CHECK(t2 == t);
    CHECK(CTime(0).GetTime() == 0);

    // GetCurrentTime is "now".
    const __time64_t now = (__time64_t)std::time(nullptr);
    const __time64_t got = CTime::GetCurrentTime().GetTime();
    CHECK(got >= now && got <= now + 2);

    // CTimeSpan components and totals.
    CTimeSpan s(1, 2, 3, 4);
    CHECK(s.GetDays() == 1 && s.GetHours() == 2 && s.GetMinutes() == 3 && s.GetSeconds() == 4);
    CHECK(s.GetTotalSeconds() == 93784);
    CHECK(s.GetTotalMinutes() == 1563);
    CHECK(s.GetTotalHours() == 26);
    CHECK(CTimeSpan(0, 0, 5, 0).GetTotalSeconds() == 300);

    // Arithmetic and comparison.
    CTime later = t + CTimeSpan(0, 0, 5, 0);
    CHECK((later - t).GetTotalSeconds() == 300);
    CHECK(later > t && t < later && later != t);
    CTime earlier = later - CTimeSpan(0, 0, 5, 0);
    CHECK(earlier == t);
    CTime acc = t;
    acc += CTimeSpan(1, 0, 0, 0);
    CHECK(acc.GetDay() == 4);
    acc -= CTimeSpan(1, 0, 0, 0);
    CHECK(acc == t);

    // Format uses strftime codes.
    CHECK(t.Format("%Y-%m-%d %H:%M:%S") == "2026-10-03 21:30:15");

    // CPoint / CSize.
    CPoint p(3, 4);
    CHECK(p.x == 3 && p.y == 4);
    p += CPoint(1, 1);
    CHECK(p == CPoint(4, 5));
    CSize sz(10, 20);
    CHECK(sz.cx == 10 && sz.cy == 20);

    // CRect.
    CRect r(10, 20, 110, 70);
    CHECK(r.Width() == 100 && r.Height() == 50);
    CHECK(r.PtInRect(CPoint(10, 20)));
    CHECK(!r.PtInRect(CPoint(110, 70)));           // right/bottom are exclusive, like Win32
    CHECK(r.CenterPoint() == CPoint(60, 45));
    CHECK(r.TopLeft() == CPoint(10, 20));
    CHECK(r.BottomRight() == CPoint(110, 70));
    r.OffsetRect(5, -5);
    CHECK(r.left == 15 && r.top == 15 && r.right == 115 && r.bottom == 65);
    r.InflateRect(2, 3);
    CHECK(r.left == 13 && r.top == 12 && r.right == 117 && r.bottom == 68);
    r.DeflateRect(2, 3);
    CHECK(r.left == 15 && r.top == 15 && r.right == 115 && r.bottom == 65);
    CHECK(CRect(0, 0, 0, 5).IsRectEmpty());
    RECT raw_rc = {1, 2, 3, 4};
    CRect fromRaw(raw_rc);
    CHECK(fromRaw.left == 1 && fromRaw.bottom == 4);
    LPRECT lp = r;                                   // CRect converts to LPRECT like MFC
    CHECK(lp->left == 15);

    if (g_failed == 0) std::printf("PASS afx_types_test\n");
    return g_failed == 0 ? 0 : 1;
}

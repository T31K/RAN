// Tests for keyboard text and IME composition routing (port/platform/text_input.h + imm.h +
// CWnd focus): the messages and strings the game's CIMEEdit sees, for Latin typing and for
// Korean composition from the macOS input method.
#include "ran_compat.h"
#include "mfc/afx_all.h"
#include "../platform/text_input.h"
#include <cstdio>
#include <string>
#include <vector>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

struct Msg { UINT msg; WPARAM w; LPARAM l; std::u16string text; };

// Records its messages; on WM_IME_COMPOSITION reads the strings like CIMEEdit does.
class Recorder : public CWnd
{
public:
    std::vector<Msg> log;
    LRESULT WindowProc(UINT m, WPARAM w, LPARAM l) override
    {
        Msg rec = { m, w, l, u"" };
        if (m == WM_IME_COMPOSITION) {
            HIMC h = ImmGetContext(m_hWnd);
            const DWORD which = (l & GCS_RESULTSTR) ? GCS_RESULTSTR : GCS_COMPSTR;
            const LONG bytes = ImmGetCompositionStringW(h, which, nullptr, 0);
            std::u16string s(bytes / 2, u'\0');
            if (bytes) ImmGetCompositionStringW(h, which, &s[0], (DWORD)bytes);
            rec.text = s;
            ImmReleaseContext(m_hWnd, h);
        }
        log.push_back(rec);
        return 0;
    }
    bool Has(UINT m) const { for (const Msg& x : log) if (x.msg == m) return true; return false; }
};

int main()
{
    Recorder main, edit;
    CWnd::MainWindow() = &main;

    // Focus moves like Windows: kill-focus to the old window, set-focus to the new one.
    CHECK(edit.SetFocus() == nullptr);
    CHECK(CWnd::GetFocus() == &edit && edit.Has(WM_SETFOCUS));
    CHECK(main.SetFocus() == &edit && edit.Has(WM_KILLFOCUS) && main.Has(WM_SETFOCUS));
    edit.SetFocus();
    edit.log.clear();
    main.log.clear();

    // Latin typing: characters as WM_CHAR to the focused window only.
    ran_platform::TextInput("ab");
    CHECK(edit.log.size() == 2 && edit.log[0].msg == WM_CHAR && edit.log[0].w == 'a' && edit.log[1].w == 'b');
    CHECK(main.log.empty());
    DWORD conv = 99, sent = 99;
    CHECK(ImmGetConversionStatus(ImmGetContext(nullptr), &conv, &sent) && conv == IME_CMODE_ALPHANUMERIC);

    // Keys: WM_KEYDOWN, plus the character TranslateMessage would make for Return/Backspace.
    edit.log.clear();
    ran_platform::TextKeyDown(VK_BACK);
    CHECK(edit.log.size() == 2 && edit.log[0].msg == WM_KEYDOWN && edit.log[0].w == VK_BACK &&
          edit.log[1].msg == WM_CHAR && edit.log[1].w == '\b');
    edit.log.clear();
    ran_platform::TextKeyDown(VK_LEFT);
    CHECK(edit.log.size() == 1 && edit.log[0].w == VK_LEFT);
    CHECK(ran_platform::SdlKeyToVk(SDLK_RETURN) == VK_RETURN && ran_platform::SdlKeyToVk(SDLK_A) == 'A' &&
          ran_platform::SdlKeyToVk(SDLK_F5) == VK_F5 && ran_platform::SdlKeyToVk(SDLK_DELETE) == VK_DELETE);

    // Korean: h -> ha -> han composed, then committed. The input method owns Backspace meanwhile.
    edit.log.clear();
    ran_platform::TextEditing("\xE3\x85\x8E");       // U+314E
    ran_platform::TextEditing("\xED\x95\x98");       // U+D558
    ran_platform::TextKeyDown(VK_BACK);              // consumed by the IME: no message
    ran_platform::TextEditing("\xED\x95\x9C");       // U+D55C
    ran_platform::TextInput("\xED\x95\x9C");         // committed
    ran_platform::TextEditing("");                   // SDL clears the composition afterwards
    CHECK(edit.log.size() == 6);
    if (edit.log.size() == 6) {
        CHECK(edit.log[0].msg == WM_IME_STARTCOMPOSITION);
        CHECK(edit.log[1].msg == WM_IME_COMPOSITION && (edit.log[1].l & GCS_COMPSTR) && edit.log[1].text == u"ㅎ");
        CHECK(edit.log[2].text == u"하");
        CHECK(edit.log[3].text == u"한" && !(edit.log[3].l & GCS_RESULTSTR));
        CHECK(edit.log[4].msg == WM_IME_COMPOSITION && (edit.log[4].l & GCS_RESULTSTR) && edit.log[4].text == u"한");
        CHECK(edit.log[5].msg == WM_IME_ENDCOMPOSITION);
    }
    CHECK(ImmGetConversionStatus(ImmGetContext(nullptr), &conv, &sent) && conv == IME_CMODE_NATIVE);
    CHECK(!ran_compat::Ime().composing && ran_compat::Ime().result.empty());

    // Composition abandoned (Esc in the input method): composition removed, then ended.
    edit.log.clear();
    ran_platform::TextEditing("\xE3\x84\xB1");       // U+3131
    ran_platform::TextEditing("");
    CHECK(edit.log.size() == 4 && edit.log[2].msg == WM_IME_COMPOSITION && edit.log[2].text.empty() &&
          edit.log[3].msg == WM_IME_ENDCOMPOSITION);

    // Committed without a preceding composition (paste, other input methods).
    edit.log.clear();
    ran_platform::TextInput("\xED\x95\x9C\xEA\xB8\x80");   // two syllables
    CHECK(edit.log.size() == 3 && edit.log[1].text == u"한글");

    // Composition queries a game might make, and the CP949 form of the result.
    ran_compat::Ime().composition = u"한";
    CHECK(ImmGetCompositionStringW(nullptr, GCS_CURSORPOS, nullptr, 0) == 1);
    BYTE attr[4] = { 9, 9, 9, 9 };
    CHECK(ImmGetCompositionStringW(nullptr, GCS_COMPATTR, attr, sizeof(attr)) == 1 && attr[0] == 0);
    char mb[8] = {};
    CHECK(ImmGetCompositionStringA(nullptr, GCS_COMPSTR, mb, sizeof(mb)) == 2 &&
          (unsigned char)mb[0] == 0xC7 && (unsigned char)mb[1] == 0xD1);
    CHECK(ImmGetProperty(nullptr, IGP_PROPERTY) & IME_PROP_AT_CARET);
    CHECK(ImmNotifyIME(ImmGetContext(nullptr), NI_COMPOSITIONSTR, CPS_CANCEL, 0) &&
          ran_compat::Ime().composition.empty() && ran_compat::Ime().cancelRequested);

    // No focused window: text goes to the main window.
    {
        Recorder temp;
        temp.SetFocus();
    }   // destroyed while focused
    CHECK(CWnd::GetFocus() == nullptr);
    main.log.clear();
    ran_platform::TextInput("x");
    CHECK(main.log.size() == 1 && main.log[0].w == 'x');

    CWnd::MainWindow() = nullptr;
    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("text_input_test: all checks passed\n");
    return 0;
}

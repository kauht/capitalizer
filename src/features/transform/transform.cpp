#include "transform.h"

#include <timeapi.h>
#include <string>

#include "utils/clipboard/clipboard.h"
#include "utils/input/input.h"
#include "utils/text/text.h"

namespace feature {
namespace {

bool g_busy = false;

std::wstring Transform(const std::wstring& s, app::Mode mode) {
    return mode == app::Mode::Upper ? util::ToUpper(s) : util::ToLower(s);
}

// Fast path: read/replace the selection with Win32 edit-control messages.
// Returns true only if a real selection was found and replaced (native
// Edit / RichEdit). Non-edit windows (browser/Electron render surfaces) report
// no selection, so this returns false and the caller uses the clipboard path.
bool TryMessageTransform(HWND ctl, app::Mode mode) {
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(ctl, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    if (selStart >= selEnd) return false;

    const int len = static_cast<int>(SendMessageW(ctl, WM_GETTEXTLENGTH, 0, 0));
    if (len <= 0) return false;

    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    const int got = static_cast<int>(SendMessageW(ctl, WM_GETTEXT, static_cast<WPARAM>(len) + 1, reinterpret_cast<LPARAM>(text.data())));
    text.resize(static_cast<size_t>(got));

    if (selEnd > static_cast<DWORD>(got)) selEnd = static_cast<DWORD>(got);
    if (selStart >= selEnd) return false;

    const std::wstring selected = text.substr(selStart, selEnd - selStart);
    const std::wstring replaced = Transform(selected, mode);

    SendMessageW(ctl, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replaced.c_str()));
    SendMessageW(ctl, EM_SETSEL, selStart, static_cast<LPARAM>(selStart + replaced.size()));
    return true;
}

// Fallback for apps with no Win32 edit control (browsers, Electron, UWP):
// copy the selection, transform it, paste it back, restore the clipboard.
void ClipboardTransform(HWND owner, app::Mode mode) {
    timeBeginPeriod(1);   // 1 ms timer resolution so the waits below are tight

    const std::wstring saved = util::GetClipboardText(owner);
    const DWORD seqBefore = GetClipboardSequenceNumber();

    if (util::ReleaseHeldModifiers()) Sleep(8);
    util::SendCtrlKey('C');

    bool copied = false;
    for (int waited = 0; waited < 400; waited += 5) {
        if (GetClipboardSequenceNumber() != seqBefore) { copied = true; break; }
        util::PumpSleep(5);
    }

    if (copied) {
        const std::wstring selection = util::GetClipboardText(owner);
        if (!selection.empty()) {
            const std::wstring replaced = Transform(selection, mode);
            if (util::SetClipboardText(owner, replaced)) {
                util::SendCtrlKey('V');
                util::PumpSleep(120);   // let the paste read our text before we restore
            }
        }
        if (!saved.empty()) util::SetClipboardText(owner, saved);
    }

    timeEndPeriod(1);
}

}  // namespace

void DoTransform(HWND clipboardOwner, app::Mode mode) {
    if (g_busy) return;
    g_busy = true;

    HWND ctl = util::FocusedControl();
    if (ctl && !TryMessageTransform(ctl, mode) && !util::IsEditControl(ctl))
        ClipboardTransform(clipboardOwner, mode);   // only non-edit controls (browser/Electron/UWP)

    g_busy = false;
}

}  // namespace feature

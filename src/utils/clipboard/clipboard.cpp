#include "clipboard.h"

#include <cstring>

namespace util {
namespace {

bool OpenClipboardRetry(HWND owner) {
    for (int i = 0; i < 200; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(2);
    }
    return false;
}

}  // namespace

std::wstring GetClipboardText(HWND owner) {
    std::wstring result;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return result;
    if (!OpenClipboardRetry(owner)) return result;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            result = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

bool SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboardRetry(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) {
        CloseClipboard();
        return false;
    }
    if (void* dst = GlobalLock(h)) {
        memcpy(dst, text.c_str(), bytes);
        GlobalUnlock(h);
    }
    const bool ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
    if (!ok) GlobalFree(h);
    CloseClipboard();
    return ok;
}

}  // namespace util

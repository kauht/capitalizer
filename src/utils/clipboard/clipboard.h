#pragma once
#include <windows.h>
#include <string>

namespace util {

    std::wstring GetClipboardText(HWND owner);
    bool SetClipboardText(HWND owner, const std::wstring& text);

}

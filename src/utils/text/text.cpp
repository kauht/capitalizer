#include "text.h"

#include <windows.h>

namespace util {
namespace {

std::wstring MapCase(const std::wstring& text, DWORD flags) {
    if (text.empty()) return text;
    const int len = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags,
                                  text.c_str(), static_cast<int>(text.size()),
                                  nullptr, 0, nullptr, nullptr, 0);
    if (len <= 0) return text;
    std::wstring out(static_cast<size_t>(len), L'\0');
    LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags,
                  text.c_str(), static_cast<int>(text.size()),
                  out.data(), len, nullptr, nullptr, 0);
    return out;
}

}  // namespace

std::wstring ToUpper(const std::wstring& text) { return MapCase(text, LCMAP_UPPERCASE); }
std::wstring ToLower(const std::wstring& text) { return MapCase(text, LCMAP_LOWERCASE); }

}  // namespace util

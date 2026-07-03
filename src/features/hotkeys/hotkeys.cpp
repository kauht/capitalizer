#include "hotkeys.h"

namespace feature {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\Capitalizer";

UINT g_upperVk = VK_PRIOR, g_upperMods = 0;
UINT g_lowerVk = VK_NEXT,  g_lowerMods = 0;
bool g_registered = false;

void SaveHotkeys() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    auto write = [&](const wchar_t* name, UINT val) {
        const DWORD d = val;
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof(d));
    };
    write(L"UpperVk", g_upperVk);   write(L"UpperMods", g_upperMods);
    write(L"LowerVk", g_lowerVk);   write(L"LowerMods", g_lowerMods);
    RegCloseKey(key);
}

}  // namespace

void LoadHotkeys() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    auto read = [&](const wchar_t* name, UINT& out) {
        DWORD d = 0, size = sizeof(d), type = 0;
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&d), &size)
                == ERROR_SUCCESS && type == REG_DWORD)
            out = d;
    };
    read(L"UpperVk", g_upperVk);   read(L"UpperMods", g_upperMods);
    read(L"LowerVk", g_lowerVk);   read(L"LowerMods", g_lowerMods);
    RegCloseKey(key);
}

void RegisterHotkeys(HWND owner) {
    const bool ok1 = RegisterHotKey(owner, kHotkeyUpper, g_upperMods | MOD_NOREPEAT, g_upperVk);
    const bool ok2 = RegisterHotKey(owner, kHotkeyLower, g_lowerMods | MOD_NOREPEAT, g_lowerVk);
    g_registered = ok1 && ok2;
}

void UnregisterHotkeys(HWND owner) {
    UnregisterHotKey(owner, kHotkeyUpper);
    UnregisterHotKey(owner, kHotkeyLower);
    g_registered = false;
}

bool HotkeysRegistered() { return g_registered; }

bool ApplyHotkeys(HWND owner, UINT uVk, UINT uMods, UINT lVk, UINT lMods) {
    if (uVk == 0 || lVk == 0) return false;
    UnregisterHotKey(owner, kHotkeyUpper);
    UnregisterHotKey(owner, kHotkeyLower);
    const bool ok1 = RegisterHotKey(owner, kHotkeyUpper, uMods | MOD_NOREPEAT, uVk);
    const bool ok2 = RegisterHotKey(owner, kHotkeyLower, lMods | MOD_NOREPEAT, lVk);
    if (ok1) UnregisterHotKey(owner, kHotkeyUpper);
    if (ok2) UnregisterHotKey(owner, kHotkeyLower);
    g_registered = false;
    if (ok1 && ok2) {
        g_upperVk = uVk; g_upperMods = uMods;
        g_lowerVk = lVk; g_lowerMods = lMods;
        SaveHotkeys();
        return true;
    }
    MessageBoxW(nullptr,
        L"That shortcut is already in use, or both hotkeys are the same. Pick another.",
        app::kName, MB_OK | MB_ICONWARNING | MB_TOPMOST);
    return false;
}

HotkeyState UpperHotkey() { return { g_upperVk, g_upperMods }; }
HotkeyState LowerHotkey() { return { g_lowerVk, g_lowerMods }; }

}  // namespace feature

#include "autostart.h"
#include <windows.h>
#include <string>

namespace feature {

    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValue[] = L"Capitalizer";

    bool IsAutostartEnabled() {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
        const LONG rc = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        return rc == ERROR_SUCCESS;
    }

    void SetAutostart(bool enable) {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &key) != ERROR_SUCCESS) return;
        if (enable) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            const std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
            RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(quoted.c_str()), static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(key, kRunValue);
        }
        RegCloseKey(key);
    }

}

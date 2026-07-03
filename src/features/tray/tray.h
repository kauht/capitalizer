#pragma once
#include <windows.h>

namespace feature {

    constexpr UINT WM_TRAYICON = WM_APP + 1;

    enum TrayCommand { kTrayAutostart = 2001, kTrayExit };

    void AddTrayIcon(HWND owner);
    void RemoveTrayIcon();
    void ShowTrayMenu(HWND owner);

}

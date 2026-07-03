#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include "app.h"
#include "features/autostart/autostart.h"
#include "features/hotkeys/hotkeys.h"
#include "features/transform/transform.h"
#include "features/tray/tray.h"
#include "ui/settings/settings.h"

// Opt into Common Controls v6 so the tray's right-click menu uses the modern themed
// look instead of the classic grey one.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

    constexpr wchar_t kClassName[] = L"CapitalizerWndClass";

    // A named system-wide mutex enforces a single running copy: a second launch finds
    // it already exists, shows a notice, and exits. The GUID just keeps the name from
    // colliding with any other program's mutex.
    constexpr wchar_t kMutexName[] = L"CapitalizerSingleInstance_{7b3f0c11-1a2b-4c3d-9e0f-abc123456789}";

    HWND g_hwnd = nullptr;
    UINT g_taskbarMsg = 0;

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_HOTKEY:
                feature::DoTransform(g_hwnd, static_cast<app::Mode>(wParam));
                return 0;

            case feature::WM_TRAYICON:
                switch (LOWORD(lParam)) {
                    case NIN_SELECT:
                    case WM_LBUTTONUP:   ui::ShowSettings(g_hwnd);      break;
                    case WM_CONTEXTMENU:
                    case WM_RBUTTONUP:   feature::ShowTrayMenu(g_hwnd); break;
                }
                return 0;

            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case feature::kTrayAutostart: feature::SetAutostart(!feature::IsAutostartEnabled()); break;
                    case feature::kTrayExit:      DestroyWindow(hwnd);                                   break;
                }
                return 0;

            case WM_DESTROY:
                feature::UnregisterHotkeys(hwnd);
                feature::RemoveTrayIcon();
                PostQuitMessage(0);
                return 0;

            default:
                if (msg == g_taskbarMsg) {
                    feature::AddTrayIcon(g_hwnd);
                    return 0;
                }
                return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Capitalizer is already running (see the system tray).", app::kName, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    feature::LoadHotkeys();

    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, kClassName, app::kName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return 1;

    feature::RegisterHotkeys(g_hwnd);
    feature::AddTrayIcon(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}

#include "settings.h"

#include <dwmapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <cwchar>
#include <string>

#include "app.h"
#include "features/autostart/autostart.h"
#include "features/hotkeys/hotkeys.h"
#include "ui/settings/settings_html.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace ui {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kSettingsClass[] = L"CapitalizerSettingsWnd";

HWND g_owner       = nullptr;
HWND g_settingsHwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_webController;
ComPtr<ICoreWebView2>           g_webView;

struct ACCENT_POLICY { int state; int flags; unsigned gradient; int anim; };
struct WINCOMPATTRDATA { int attrib; void* data; size_t size; };
using SetWindowCompositionAttributeFn = BOOL (WINAPI*)(HWND, WINCOMPATTRDATA*);

void ApplyAcrylic(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setca = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));
        if (setca) {
            ACCENT_POLICY accent = { 4, 0, 0xC8141418, 0 };  // acrylic blur + dark ABGR tint
            WINCOMPATTRDATA data = { 19, &accent, sizeof(accent) };
            setca(hwnd, &data);
        }
    }
}

void SendInitToWeb() {
    if (!g_webView) return;
    const feature::HotkeyState u = feature::UpperHotkey();
    const feature::HotkeyState l = feature::LowerHotkey();
    wchar_t buf[128];
    swprintf_s(buf, L"init|%u|%u|%u|%u|%d",
               u.vk, u.mods, l.vk, l.mods, feature::IsAutostartEnabled() ? 1 : 0);
    g_webView->PostWebMessageAsString(buf);
}

void OnWebMessage(LPCWSTR msg) {
    if (wcscmp(msg, L"ready") == 0) { SendInitToWeb(); return; }
    if (wcscmp(msg, L"cancel") == 0) { if (g_settingsHwnd) DestroyWindow(g_settingsHwnd); return; }
    if (wcscmp(msg, L"minimize") == 0) { if (g_settingsHwnd) ShowWindow(g_settingsHwnd, SW_MINIMIZE); return; }
    if (wcscmp(msg, L"drag") == 0) {
        if (g_settingsHwnd) { ReleaseCapture(); SendMessageW(g_settingsHwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return;
    }
    if (wcsncmp(msg, L"apply|", 6) == 0) {
        UINT uv = 0, um = 0, lv = 0, lm = 0; int as = 0;
        if (swscanf_s(msg + 6, L"%u|%u|%u|%u|%d", &uv, &um, &lv, &lm, &as) == 5) {
            if (feature::ApplyHotkeys(g_owner, uv, um, lv, lm)) {
                feature::SetAutostart(as != 0);
            } else if (g_webView) {
                SendInitToWeb();   // conflict: revert the display to the last-good values
            }
        }
    }
}

void CreateWebView() {
    std::wstring udf;
    wchar_t local[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH))
        udf = std::wstring(local) + L"\\Capitalizer";

    CreateCoreWebView2EnvironmentWithOptions(nullptr, udf.empty() ? nullptr : udf.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT, ICoreWebView2Environment* env) -> HRESULT {
                if (!env || !g_settingsHwnd) return S_OK;
                env->CreateCoreWebView2Controller(g_settingsHwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!controller || !g_settingsHwnd) return S_OK;
                            g_webController = controller;
                            g_webController->get_CoreWebView2(&g_webView);

                            ComPtr<ICoreWebView2Controller2> c2;
                            if (SUCCEEDED(g_webController.As(&c2))) {
                                COREWEBVIEW2_COLOR clear = { 0, 0, 0, 0 };
                                c2->put_DefaultBackgroundColor(clear);
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            g_webView->get_Settings(&settings);
                            if (settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                ComPtr<ICoreWebView2Settings3> s3;
                                if (SUCCEEDED(settings.As(&s3)))
                                    s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
                            }

                            EventRegistrationToken tok;
                            g_webView->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR s = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&s)) && s) {
                                            OnWebMessage(s);
                                            CoTaskMemFree(s);
                                        }
                                        return S_OK;
                                    }).Get(), &tok);

                            RECT rc;
                            GetClientRect(g_settingsHwnd, &rc);
                            g_webController->put_Bounds(rc);
                            g_webView->NavigateToString(kSettingsHtml);
                            g_webController->put_IsVisible(TRUE);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (g_webController) {
                RECT rc; GetClientRect(hwnd, &rc);
                g_webController->put_Bounds(rc);
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_webController) g_webController->Close();
            g_webController.Reset();
            g_webView.Reset();
            g_settingsHwnd = nullptr;
            if (!feature::HotkeysRegistered()) feature::RegisterHotkeys(g_owner);   // restore suspended hotkeys
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

void ShowSettings(HWND owner) {
    g_owner = owner;
    if (g_settingsHwnd) {
        SetForegroundWindow(g_settingsHwnd);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = SettingsWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kSettingsClass;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }

    const UINT dpi = GetDpiForSystem();
    // Frameless: the title bar is drawn in HTML (see kSettingsHtml). WS_MINIMIZEBOX
    // keeps the taskbar minimize working; WS_EX_APPWINDOW gives it a taskbar button.
    const DWORD style = WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX;
    const int w = MulDiv(480, dpi, 96);
    const int h = MulDiv(260, dpi, 96);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + (work.right - work.left - w) / 2;
    const int y = work.top + (work.bottom - work.top - h) / 2;

    const HMODULE inst = GetModuleHandleW(nullptr);
    g_settingsHwnd = CreateWindowExW(WS_EX_APPWINDOW, kSettingsClass, app::kName,
                                     style, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!g_settingsHwnd) return;

    SendMessageW(g_settingsHwnd, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(LoadIconW(inst, MAKEINTRESOURCEW(1))));
    SendMessageW(g_settingsHwnd, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1),
                     IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                     LR_DEFAULTCOLOR))));

    feature::UnregisterHotkeys(owner);   // let the key picker capture Page Up/Down etc. directly
    ApplyAcrylic(g_settingsHwnd);
    ShowWindow(g_settingsHwnd, SW_SHOW);
    SetForegroundWindow(g_settingsHwnd);
    CreateWebView();
}

}  // namespace ui

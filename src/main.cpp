#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <timeapi.h>
#include <cwchar>
#include <string>
#include "resource.h"

// Enable Common Controls v6 so the dialog and its Hot Key controls use the
// modern Windows theme.
#pragma comment(linker, "/manifestdependency:\"type='win32' "               \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "           \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr wchar_t kClassName[] = L"CapitalizerWndClass";
constexpr wchar_t kAppName[]   = L"Capitalizer";
constexpr wchar_t kRunValue[]  = L"Capitalizer";
constexpr wchar_t kRunKey[]      = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kSettingsKey[] = L"Software\\Capitalizer";
constexpr wchar_t kMutexName[]   = L"CapitalizerSingleInstance_{7b3f0c11-1a2b-4c3d-9e0f-abc123456789}";

constexpr UINT WM_TRAYICON = WM_APP + 1;

enum class Mode { Upper, Lower };

constexpr int HK_UPPER = static_cast<int>(Mode::Upper);
constexpr int HK_LOWER = static_cast<int>(Mode::Lower);

enum {
    ID_AUTOSTART = 2001,
    ID_EXIT,
};

HWND            g_hwnd        = nullptr;
NOTIFYICONDATAW g_nid         = {};
bool            g_busy        = false;
bool            g_settingsOpen = false;
UINT            g_taskbarMsg  = 0;

// Current hotkeys (MOD_* flags without MOD_NOREPEAT). Defaults: Page Up / Page Down.
UINT g_upperVk = VK_PRIOR, g_upperMods = 0;
UINT g_lowerVk = VK_NEXT,  g_lowerMods = 0;

std::wstring MapCase(const std::wstring& s, DWORD flags) {
    if (s.empty()) return s;
    const int len = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags,
                                  s.c_str(), static_cast<int>(s.size()),
                                  nullptr, 0, nullptr, nullptr, 0);
    if (len <= 0) return s;
    std::wstring out(static_cast<size_t>(len), L'\0');
    LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags,
                  s.c_str(), static_cast<int>(s.size()),
                  out.data(), len, nullptr, nullptr, 0);
    return out;
}

std::wstring Transform(const std::wstring& s, Mode mode) {
    switch (mode) {
        case Mode::Upper: return MapCase(s, LCMAP_UPPERCASE);
        case Mode::Lower: return MapCase(s, LCMAP_LOWERCASE);
    }
    return s;
}

HWND GetFocusedControl() {
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;

    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(gti);
    const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus)
        return gti.hwndFocus;
    return fg;
}

// Fast path: read/replace the selection with Win32 edit-control messages.
// Returns true only if a real selection was found and replaced (native
// Edit / RichEdit). Non-edit windows (browser/Electron render surfaces) report
// no selection, so this returns false and the caller uses the clipboard path.
bool TryMessageTransform(HWND ctl, Mode mode) {
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

bool OpenClipboardRetry() {
    for (int i = 0; i < 200; ++i) {
        if (OpenClipboard(g_hwnd)) return true;
        Sleep(2);
    }
    return false;
}

std::wstring GetClipboardText() {
    std::wstring result;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return result;
    if (!OpenClipboardRetry()) return result;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            result = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

bool SetClipboardText(const std::wstring& text) {
    if (!OpenClipboardRetry()) return false;
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

bool NeutralizeModifiers() {
    const WORD mods[] = {
        VK_LMENU, VK_RMENU, VK_MENU,
        VK_LSHIFT, VK_RSHIFT, VK_SHIFT,
        VK_LCONTROL, VK_RCONTROL, VK_CONTROL,
        VK_LWIN, VK_RWIN,
    };
    INPUT in[sizeof(mods) / sizeof(mods[0])] = {};
    int n = 0;
    for (WORD vk : mods) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            in[n].type       = INPUT_KEYBOARD;
            in[n].ki.wVk     = vk;
            in[n].ki.dwFlags = KEYEVENTF_KEYUP;
            ++n;
        }
    }
    if (n) SendInput(n, in, sizeof(INPUT));
    return n > 0;
}

void SendCtrlKey(WORD vk) {
    INPUT in[4] = {};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vk;
    in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = vk;         in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

bool IsEditClass(HWND h) {
    wchar_t cls[64] = {};
    if (GetClassNameW(h, cls, 64) <= 0) return false;
    CharLowerW(cls);
    return wcsstr(cls, L"edit") != nullptr;
}

// Sleep while still dispatching messages. After we write the clipboard we are
// its owner, so when another app (e.g. a browser) empties the clipboard on its
// next copy it SendMessage()s us WM_DESTROYCLIPBOARD; if we were blocked in a
// plain Sleep we'd deadlock its copy. Pumping keeps us responsive. Re-entrant
// hotkeys are guarded by g_busy, so this is safe.
void PumpSleep(int ms) {
    for (int waited = 0; waited < ms; waited += 5) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}

// Fallback for apps with no Win32 edit control (browsers, Electron, UWP):
// copy the selection, transform it, paste it back, restore the clipboard.
void ClipboardTransform(Mode mode) {
    timeBeginPeriod(1);   // 1 ms timer resolution so the waits below are tight

    const std::wstring saved = GetClipboardText();
    const DWORD seqBefore = GetClipboardSequenceNumber();

    if (NeutralizeModifiers()) Sleep(8);
    SendCtrlKey('C');

    bool copied = false;
    for (int waited = 0; waited < 400; waited += 5) {
        if (GetClipboardSequenceNumber() != seqBefore) { copied = true; break; }
        PumpSleep(5);
    }

    if (copied) {
        const std::wstring selection = GetClipboardText();
        if (!selection.empty()) {
            const std::wstring replaced = Transform(selection, mode);
            if (SetClipboardText(replaced)) {
                SendCtrlKey('V');
                PumpSleep(120);   // let the paste read our text before we restore
            }
        }
        if (!saved.empty()) SetClipboardText(saved);
    }

    timeEndPeriod(1);
}

void DoTransform(Mode mode) {
    if (g_busy) return;
    g_busy = true;

    HWND ctl = GetFocusedControl();
    if (ctl && !TryMessageTransform(ctl, mode) && !IsEditClass(ctl))
        ClipboardTransform(mode);   // only non-edit controls (browser/Electron/UWP)

    g_busy = false;
}

bool IsAutostartEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    const LONG rc = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

void SetAutostart(bool enable) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        const std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(key, kRunValue, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(quoted.c_str()),
                       static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
}

void AddTrayIcon() {
    g_nid = {};
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(g_nid.szTip, L"Capitalizer — left-click for settings", 128);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

void ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : MF_UNCHECKED),
                ID_AUTOSTART, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

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

void SaveHotkeys() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    auto write = [&](const wchar_t* name, UINT val) {
        const DWORD d = val;
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&d), sizeof(d));
    };
    write(L"UpperVk", g_upperVk);   write(L"UpperMods", g_upperMods);
    write(L"LowerVk", g_lowerVk);   write(L"LowerMods", g_lowerMods);
    RegCloseKey(key);
}

void RegisterHotkeys() {
    RegisterHotKey(g_hwnd, HK_UPPER, g_upperMods | MOD_NOREPEAT, g_upperVk);
    RegisterHotKey(g_hwnd, HK_LOWER, g_lowerMods | MOD_NOREPEAT, g_lowerVk);
}

// The Hot Key control speaks HOTKEYF_* flags; RegisterHotKey wants MOD_*.
UINT HotkeyfToMod(BYTE hkf) {
    UINT m = 0;
    if (hkf & HOTKEYF_ALT)     m |= MOD_ALT;
    if (hkf & HOTKEYF_CONTROL) m |= MOD_CONTROL;
    if (hkf & HOTKEYF_SHIFT)   m |= MOD_SHIFT;
    return m;
}

BYTE ModToHotkeyf(UINT mods) {
    BYTE h = 0;
    if (mods & MOD_ALT)     h |= HOTKEYF_ALT;
    if (mods & MOD_CONTROL) h |= HOTKEYF_CONTROL;
    if (mods & MOD_SHIFT)   h |= HOTKEYF_SHIFT;
    return h;
}

// Navigation keys are "extended"; the Hot Key control needs HOTKEYF_EXT to show
// their proper name (e.g. "Page Up" instead of "Num 9").
bool IsExtendedVk(UINT vk) {
    switch (vk) {
        case VK_PRIOR: case VK_NEXT:  case VK_END:    case VK_HOME:
        case VK_LEFT:  case VK_UP:    case VK_RIGHT:  case VK_DOWN:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            return true;
        default:
            return false;
    }
}

// Try to register new hotkeys; on any failure roll back to the current ones.
bool ApplyHotkeys(UINT uVk, UINT uMods, UINT lVk, UINT lMods) {
    if (uVk == 0 || lVk == 0) {
        MessageBoxW(nullptr, L"Please choose a key for both hotkeys.",
                    kAppName, MB_OK | MB_ICONWARNING);
        return false;
    }
    UnregisterHotKey(g_hwnd, HK_UPPER);
    UnregisterHotKey(g_hwnd, HK_LOWER);
    const bool ok1 = RegisterHotKey(g_hwnd, HK_UPPER, uMods | MOD_NOREPEAT, uVk);
    const bool ok2 = RegisterHotKey(g_hwnd, HK_LOWER, lMods | MOD_NOREPEAT, lVk);
    if (!ok1 || !ok2) {
        if (ok1) UnregisterHotKey(g_hwnd, HK_UPPER);
        if (ok2) UnregisterHotKey(g_hwnd, HK_LOWER);
        RegisterHotkeys();   // restore the previous (still-current) globals
        MessageBoxW(nullptr,
            L"That shortcut is already in use, or both hotkeys are the same. "
            L"Keeping your previous hotkeys.",
            kAppName, MB_OK | MB_ICONWARNING);
        return false;
    }
    g_upperVk = uVk; g_upperMods = uMods;
    g_lowerVk = lVk; g_lowerMods = lMods;
    SaveHotkeys();
    return true;
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            SendMessageW(dlg, WM_SETICON, ICON_SMALL,
                         reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_APPLICATION)));
            SetForegroundWindow(dlg);
            BYTE uh = ModToHotkeyf(g_upperMods);
            BYTE lh = ModToHotkeyf(g_lowerMods);
            if (IsExtendedVk(g_upperVk)) uh |= HOTKEYF_EXT;
            if (IsExtendedVk(g_lowerVk)) lh |= HOTKEYF_EXT;
            SendDlgItemMessageW(dlg, IDC_HK_UPPER, HKM_SETHOTKEY, MAKEWORD(g_upperVk, uh), 0);
            SendDlgItemMessageW(dlg, IDC_HK_LOWER, HKM_SETHOTKEY, MAKEWORD(g_lowerVk, lh), 0);
            CheckDlgButton(dlg, IDC_AUTOSTART,
                           IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    const WORD u = static_cast<WORD>(
                        SendDlgItemMessageW(dlg, IDC_HK_UPPER, HKM_GETHOTKEY, 0, 0));
                    const WORD l = static_cast<WORD>(
                        SendDlgItemMessageW(dlg, IDC_HK_LOWER, HKM_GETHOTKEY, 0, 0));
                    if (ApplyHotkeys(LOBYTE(u), HotkeyfToMod(HIBYTE(u)),
                                     LOBYTE(l), HotkeyfToMod(HIBYTE(l)))) {
                        SetAutostart(IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED);
                        EndDialog(dlg, IDOK);
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

void ShowSettings() {
    if (g_settingsOpen) return;   // one dialog at a time
    g_settingsOpen = true;
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS),
                    g_hwnd, SettingsProc, 0);
    g_settingsOpen = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            DoTransform(static_cast<Mode>(wParam));
            return 0;

        case WM_TRAYICON:
            switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case WM_LBUTTONUP:   ShowSettings(); break;
                case WM_CONTEXTMENU:
                case WM_RBUTTONUP:   ShowTrayMenu(); break;
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_AUTOSTART: SetAutostart(!IsAutostartEnabled()); break;
                case ID_EXIT:      DestroyWindow(hwnd);                 break;
            }
            return 0;

        case WM_DESTROY:
            for (int id : {HK_UPPER, HK_LOWER})
                UnregisterHotKey(hwnd, id);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;

        default:
            if (msg == g_taskbarMsg) {
                AddTrayIcon();
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Capitalizer is already running (see the system tray).",
                    kAppName, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    LoadHotkeys();

    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, kClassName, kAppName, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return 1;

    RegisterHotkeys();
    AddTrayIcon();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <timeapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <cwchar>
#include <string>

#pragma comment(linker, "/manifestdependency:\"type='win32' "               \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "           \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

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
UINT            g_taskbarMsg  = 0;

// Current hotkeys (MOD_* flags without MOD_NOREPEAT). Defaults: Page Up / Page Down.
UINT g_upperVk = VK_PRIOR, g_upperMods = 0;
UINT g_lowerVk = VK_NEXT,  g_lowerMods = 0;
bool g_hkRegistered = false;

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
    g_nid.hIcon            = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
    const bool ok1 = RegisterHotKey(g_hwnd, HK_UPPER, g_upperMods | MOD_NOREPEAT, g_upperVk);
    const bool ok2 = RegisterHotKey(g_hwnd, HK_LOWER, g_lowerMods | MOD_NOREPEAT, g_lowerVk);
    g_hkRegistered = ok1 && ok2;
}

// Global hotkeys are suspended while the settings window is open, so its key
// picker receives the raw keypress instead of the hotkey firing a transform.
void UnregisterHotkeys() {
    UnregisterHotKey(g_hwnd, HK_UPPER);
    UnregisterHotKey(g_hwnd, HK_LOWER);
    g_hkRegistered = false;
}

// Commit the new hotkeys chosen in the settings window (hotkeys are unregistered
// while it is open). Registers them; on conflict keeps the old ones and warns.
bool ApplyHotkeys(UINT uVk, UINT uMods, UINT lVk, UINT lMods) {
    if (uVk == 0 || lVk == 0) return false;
    UnregisterHotKey(g_hwnd, HK_UPPER);
    UnregisterHotKey(g_hwnd, HK_LOWER);
    const bool ok1 = RegisterHotKey(g_hwnd, HK_UPPER, uMods | MOD_NOREPEAT, uVk);
    const bool ok2 = RegisterHotKey(g_hwnd, HK_LOWER, lMods | MOD_NOREPEAT, lVk);
    if (ok1 && ok2) {
        g_upperVk = uVk; g_upperMods = uMods;
        g_lowerVk = lVk; g_lowerMods = lMods;
        g_hkRegistered = true;
        SaveHotkeys();
        return true;
    }
    if (ok1) UnregisterHotKey(g_hwnd, HK_UPPER);
    if (ok2) UnregisterHotKey(g_hwnd, HK_LOWER);
    g_hkRegistered = false;
    MessageBoxW(nullptr,
        L"That shortcut is already in use, or both hotkeys are the same. Pick another.",
        kAppName, MB_OK | MB_ICONWARNING | MB_TOPMOST);
    return false;
}

constexpr wchar_t kSettingsClass[] = L"CapitalizerSettingsWnd";

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

HWND g_settingsHwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_webController;
ComPtr<ICoreWebView2>           g_webView;

constexpr wchar_t kSettingsHtml[] = LR"HTML(<!doctype html><html><head><meta charset="utf-8"><style>
:root { color-scheme: dark; }
* { box-sizing: border-box; margin: 0; padding: 0; }
html, body { height: 100%; }
body {
  font-family: 'Segoe UI Variable Text','Segoe UI',system-ui,sans-serif;
  color: #e6e6ea; background: transparent; overflow: hidden;
  user-select: none; -webkit-user-select: none;
}
.panel { height: 100%; display: flex; flex-direction: column;
  padding: 20px 22px 16px; background: rgba(18,18,22,0.86); }
.header { margin-bottom: 14px; display: flex; align-items: center; gap: 12px; }
.logo { width: 48px; height: 48px; border-radius: 12px; flex: none; }
.title { font-size: 15px; font-weight: 600; letter-spacing: .2px; }
.subtitle { font-size: 12px; color: #9a9aa4; margin-top: 3px; }
.rows { display: flex; flex-direction: column; }
.row { display: flex; align-items: center; justify-content: space-between;
  padding: 12px 0; border-bottom: 1px solid rgba(255,255,255,0.05); }
.label { font-size: 13px; color: #d6d6db; }
.hk { min-width: 170px; text-align: center; font-size: 12.5px; color: #e6e6ea;
  padding: 8px 12px; background: rgba(255,255,255,0.055);
  border: 1px solid rgba(255,255,255,0.09); border-radius: 8px; cursor: pointer;
  transition: background .12s, border-color .12s; outline: none; }
.hk:hover { background: rgba(255,255,255,0.09); }
.hk.capturing { border-color: #4c9bf5; background: rgba(76,155,245,0.14); color: #bcd8fb; }
.toggle { width: 42px; height: 24px; border-radius: 999px; background: rgba(255,255,255,0.16);
  position: relative; cursor: pointer; transition: background .16s; outline: none; }
.toggle .knob { position: absolute; top: 3px; left: 3px; width: 18px; height: 18px;
  border-radius: 50%; background: #fff; transition: transform .16s; box-shadow: 0 1px 2px rgba(0,0,0,.4); }
.toggle.on { background: #2c7df0; }
.toggle.on .knob { transform: translateX(18px); }
.footer { margin-top: auto; padding-top: 14px; display: flex; align-items: center; gap: 8px; }
.hint { font-size: 11px; color: #6f6f78; margin-right: auto; }
.btn { font-family: inherit; font-size: 13px; padding: 7px 18px; border-radius: 8px; border: none;
  background: rgba(255,255,255,0.08); color: #e6e6ea; cursor: pointer; transition: background .12s; }
.btn:hover { background: rgba(255,255,255,0.14); }
.btn.primary { background: #2c7df0; color: #fff; }
.btn.primary:hover { background: #3b8bff; }
</style></head><body>
<div class="panel">
  <div class="header">
    <img class="logo" src="data:image/png;base64,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">
    <div>
      <div class="title">Capitalizer</div>
      <div class="subtitle">Set the hotkeys that change your selected text's case.</div>
    </div>
  </div>
  <div class="rows">
    <div class="row"><div class="label">UPPERCASE</div><div class="hk" id="hkU" tabindex="0">Unset</div></div>
    <div class="row"><div class="label">lowercase</div><div class="hk" id="hkL" tabindex="0">Unset</div></div>
    <div class="row" style="border-bottom:none"><div class="label">Start with Windows</div>
      <div class="toggle" id="tg" tabindex="0"><div class="knob"></div></div></div>
  </div>
  <div class="footer">
    <div class="hint">Click a field, then press your shortcut</div>
    <button class="btn primary" id="save">Save</button>
  </div>
</div>
<script>
var vp = window.chrome.webview;
var st = { hkU:{vk:0,mods:0}, hkL:{vk:0,mods:0}, autostart:false };
var capturing = null;
function keyName(vk){
  var m={8:'Backspace',9:'Tab',13:'Enter',20:'Caps Lock',27:'Esc',32:'Space',
    33:'Page Up',34:'Page Down',35:'End',36:'Home',37:'Left',38:'Up',39:'Right',40:'Down',
    45:'Insert',46:'Delete',
    112:'F1',113:'F2',114:'F3',115:'F4',116:'F5',117:'F6',118:'F7',119:'F8',120:'F9',121:'F10',122:'F11',123:'F12',
    186:';',187:'=',188:',',189:'-',190:'.',191:'/',192:'`',219:'[',220:'\\',221:']',222:"'"};
  if(m[vk]) return m[vk];
  if(vk>=65&&vk<=90) return String.fromCharCode(vk);
  if(vk>=48&&vk<=57) return String.fromCharCode(vk);
  if(vk>=96&&vk<=105) return 'Num '+(vk-96);
  return 'Key '+vk;
}
function combo(o){
  if(!o.vk) return 'Unset';
  var s='';
  if(o.mods&2) s+='Ctrl + ';
  if(o.mods&1) s+='Alt + ';
  if(o.mods&4) s+='Shift + ';
  return s+keyName(o.vk);
}
function render(){
  var u=document.getElementById('hkU'), l=document.getElementById('hkL'), t=document.getElementById('tg');
  u.textContent = capturing==='hkU' ? 'Press a key...' : combo(st.hkU);
  l.textContent = capturing==='hkL' ? 'Press a key...' : combo(st.hkL);
  u.classList.toggle('capturing', capturing==='hkU');
  l.classList.toggle('capturing', capturing==='hkL');
  t.classList.toggle('on', st.autostart);
}
function startCap(w){ capturing=w; render(); document.getElementById(w).focus(); }
function stopCap(){ capturing=null; render(); }
document.getElementById('hkU').onclick=function(){ startCap('hkU'); };
document.getElementById('hkL').onclick=function(){ startCap('hkL'); };
document.getElementById('tg').onclick=function(){ st.autostart=!st.autostart; render(); };
document.getElementById('save').onclick=doSave;
function doSave(){ vp.postMessage('save|'+st.hkU.vk+'|'+st.hkU.mods+'|'+st.hkL.vk+'|'+st.hkL.mods+'|'+(st.autostart?1:0)); }
document.addEventListener('keydown', function(e){
  if(capturing){
    e.preventDefault();
    var vk=e.keyCode;
    if(vk===16||vk===17||vk===18||vk===91||vk===92) return;
    if(vk===27){ stopCap(); return; }
    st[capturing]={vk:vk, mods:(e.ctrlKey?2:0)|(e.altKey?1:0)|(e.shiftKey?4:0)};
    stopCap();
    return;
  }
  if(e.key==='Enter') doSave();
  else if(e.key==='Escape') vp.postMessage('cancel');
});
vp.addEventListener('message', function(e){
  var p=(''+e.data).split('|');
  if(p[0]==='init'){
    st.hkU={vk:+p[1],mods:+p[2]};
    st.hkL={vk:+p[3],mods:+p[4]};
    st.autostart=(p[5]==='1');
    render();
  }
});
render();
vp.postMessage('ready');
</script>
</body></html>)HTML";

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
            ACCENT_POLICY accent = { 4, 0, 0xE0161618, 0 };  // acrylic blur + dark ABGR tint
            WINCOMPATTRDATA data = { 19, &accent, sizeof(accent) };
            setca(hwnd, &data);
        }
    }
}

void SendInitToWeb() {
    if (!g_webView) return;
    wchar_t buf[128];
    swprintf_s(buf, L"init|%u|%u|%u|%u|%d",
               g_upperVk, g_upperMods, g_lowerVk, g_lowerMods, IsAutostartEnabled() ? 1 : 0);
    g_webView->PostWebMessageAsString(buf);
}

void OnWebMessage(LPCWSTR msg) {
    if (wcscmp(msg, L"ready") == 0) { SendInitToWeb(); return; }
    if (wcscmp(msg, L"cancel") == 0) { if (g_settingsHwnd) DestroyWindow(g_settingsHwnd); return; }
    if (wcsncmp(msg, L"save|", 5) == 0) {
        UINT uv = 0, um = 0, lv = 0, lm = 0; int as = 0;
        if (swscanf_s(msg + 5, L"%u|%u|%u|%u|%d", &uv, &um, &lv, &lm, &as) == 5) {
            if (ApplyHotkeys(uv, um, lv, lm)) {
                SetAutostart(as != 0);
                if (g_settingsHwnd) DestroyWindow(g_settingsHwnd);
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
            if (!g_hkRegistered) RegisterHotkeys();   // restore suspended hotkeys
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSettings() {
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
    const DWORD style = WS_CAPTION | WS_SYSMENU;
    RECT rc = { 0, 0, MulDiv(480, dpi, 96), MulDiv(300, dpi, 96) };
    AdjustWindowRectExForDpi(&rc, style, FALSE, WS_EX_TOPMOST, dpi);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + (work.right - work.left - w) / 2;
    const int y = work.top + (work.bottom - work.top - h) / 2;

    const HMODULE inst = GetModuleHandleW(nullptr);
    g_settingsHwnd = CreateWindowExW(WS_EX_TOPMOST, kSettingsClass, L"Capitalizer",
                                     style, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!g_settingsHwnd) return;

    SendMessageW(g_settingsHwnd, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(LoadIconW(inst, MAKEINTRESOURCEW(1))));
    SendMessageW(g_settingsHwnd, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1),
                     IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                     LR_DEFAULTCOLOR))));

    UnregisterHotkeys();   // let the key picker capture Page Up/Down etc. directly
    ApplyAcrylic(g_settingsHwnd);
    ShowWindow(g_settingsHwnd, SW_SHOW);
    SetForegroundWindow(g_settingsHwnd);
    CreateWebView();
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
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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

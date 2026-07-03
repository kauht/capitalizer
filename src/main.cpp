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
    RegisterHotKey(g_hwnd, HK_UPPER, g_upperMods | MOD_NOREPEAT, g_upperVk);
    RegisterHotKey(g_hwnd, HK_LOWER, g_lowerMods | MOD_NOREPEAT, g_lowerVk);
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
  padding: 20px 22px 16px; background: rgba(20,20,24,0.12); }
.header { margin-bottom: 14px; display: flex; align-items: center; gap: 12px; }
.logo { width: 36px; height: 36px; border-radius: 9px; flex: none; }
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
    <svg class="logo" viewBox="0 0 36 36" xmlns="http://www.w3.org/2000/svg"><rect x="1.5" y="1.5" width="33" height="33" rx="9" fill="#2a2f3a" stroke="rgba(255,255,255,0.12)"/><path d="M13 8 V28 M13 18 L24 8 M13 18 L24 28" stroke="#9fb6dc" stroke-width="3.2" stroke-linecap="round" stroke-linejoin="round" fill="none"/></svg>
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
    <button class="btn" id="cancel">Cancel</button>
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
document.getElementById('cancel').onclick=function(){ vp.postMessage('cancel'); };
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
            ACCENT_POLICY accent = { 4, 0, 0xB0161618, 0 };  // acrylic blur + dark ABGR tint
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
    const int w = MulDiv(480, dpi, 96);
    const int h = MulDiv(300, dpi, 96);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + (work.right - work.left - w) / 2;
    const int y = work.top + (work.bottom - work.top - h) / 2;

    g_settingsHwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsClass, L"Capitalizer",
                                     WS_POPUP, x, y, w, h, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
    if (!g_settingsHwnd) return;

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

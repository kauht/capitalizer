#include "input.h"

namespace util {

    HWND FocusedControl() {
        HWND fg = GetForegroundWindow();
        if (!fg) return nullptr;

        GUITHREADINFO gti = {};
        gti.cbSize = sizeof(gti);
        const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
        if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) return gti.hwndFocus;
        return fg;
    }

    bool IsEditControl(HWND control) {
        wchar_t cls[64] = {};
        if (GetClassNameW(control, cls, 64) <= 0) return false;
        CharLowerW(cls);
        return wcsstr(cls, L"edit") != nullptr;
    }

    bool ReleaseHeldModifiers() {
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
                in[n].type = INPUT_KEYBOARD;
                in[n].ki.wVk = vk;
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

}

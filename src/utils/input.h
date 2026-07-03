#pragma once
#include <windows.h>

namespace util {

// The control with keyboard focus in the foreground window, or the window itself.
HWND FocusedControl();

// True if the window's class name contains "edit" (native Edit / RichEdit).
bool IsEditControl(HWND control);

// Release any physically-held modifier keys so a synthetic shortcut isn't polluted.
bool ReleaseHeldModifiers();

// Send a Ctrl+<vk> chord via SendInput.
void SendCtrlKey(WORD vk);

// Sleep while pumping messages. After we own the clipboard, another app emptying
// it sends us WM_DESTROYCLIPBOARD synchronously; a plain Sleep would deadlock its
// copy, so we keep dispatching.
void PumpSleep(int ms);

}

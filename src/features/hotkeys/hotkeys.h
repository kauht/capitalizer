#pragma once
#include <windows.h>

#include "app.h"

namespace feature {

// Hotkey command ids double as the transform mode (see the WM_HOTKEY handler).
constexpr int kHotkeyUpper = static_cast<int>(app::Mode::Upper);
constexpr int kHotkeyLower = static_cast<int>(app::Mode::Lower);

struct HotkeyState { UINT vk; UINT mods; };

void LoadHotkeys();
void RegisterHotkeys(HWND owner);

// Suspended while the settings window is open so its key picker receives the
// raw keypress instead of the hotkey firing a transform.
void UnregisterHotkeys(HWND owner);
bool HotkeysRegistered();

// Validate and persist the hotkeys chosen in settings. Registers them to check
// for conflicts, then immediately unregisters (they stay suspended while the
// window is open and are re-registered on close). Warns and returns false on
// conflict, leaving the current values untouched.
bool ApplyHotkeys(HWND owner, UINT uVk, UINT uMods, UINT lVk, UINT lMods);

HotkeyState UpperHotkey();
HotkeyState LowerHotkey();

}

#pragma once
#include <windows.h>

namespace ui {

// Open (or focus) the frameless settings window. `owner` is the app's message
// window; its global hotkeys are suspended while settings is open and restored
// when it closes.
void ShowSettings(HWND owner);

}

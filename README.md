# Capitalizer

A tiny always-on Windows tray app that changes the case of the **currently
selected text** in any application via global hotkeys.

| Hotkey      | Result     |
|-------------|------------|
| `Page Up`   | UPPERCASE  |
| `Page Down` | lowercase  |

Select text anywhere, press the key, and the selection is transformed in place.

- In native Windows text fields (`Edit` / `RichEdit`) it edits the selection
  directly with Win32 messages.
- In apps that draw their own text (browsers, Electron/VS Code, UWP) it falls
  back to copy → transform → paste, then restores your clipboard.

Right-click the tray icon for options (a `Ctrl+Shift+Right-Click` uppercase
toggle, "Start with Windows", and Exit).

## Build

Requires [xmake](https://xmake.io) and MSVC (Visual Studio 2022/2026 or Build Tools).

```powershell
xmake        # builds build/windows/x64/release/capitalizer.exe
xmake run    # or just run the .exe
```

Native C++23 / Win32, no dependencies. The CRT is linked statically, so the
`.exe` runs standalone.

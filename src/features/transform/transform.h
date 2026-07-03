#pragma once
#include <windows.h>

#include "app.h"

namespace feature {

    // Change the case of the current selection in the foreground application.
    // `clipboardOwner` owns the clipboard during the copy/paste fallback path.
    void DoTransform(HWND clipboardOwner, app::Mode mode);

}

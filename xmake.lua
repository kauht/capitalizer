set_project("capitalizer")
set_xmakever("2.9.0")

add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

target("capitalizer")
    set_kind("binary")
    add_files("src/**.cpp", "src/app.rc")
    add_includedirs("src")

    -- Vendored WebView2 SDK, statically linked so the .exe is standalone.
    add_includedirs("vendor/webview2/include")
    add_linkdirs("vendor/webview2/lib")
    add_links("WebView2LoaderStatic")

    add_syslinks("user32", "shell32", "advapi32", "winmm", "dwmapi",
                 "ole32", "oleaut32", "version")

    set_runtimes(is_mode("debug") and "MTd" or "MT")

    add_defines("UNICODE", "_UNICODE", "WIN32_LEAN_AND_MEAN", "NOMINMAX",
                "WINVER=0x0A00", "_WIN32_WINNT=0x0A00")
    add_cxflags("/utf-8")

    -- GUI subsystem so no console window appears.
    add_ldflags("/subsystem:windows", {force = true})

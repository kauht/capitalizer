set_project("capitalizer")
set_xmakever("2.9.0")

add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

target("capitalizer")
    set_kind("binary")
    add_files("src/*.cpp", "src/utils/*.cpp", "src/app.rc")
    add_includedirs("src")

    -- Vendored WebView2 SDK (headers + dynamic loader import lib).
    add_includedirs("vendor/webview2/include")
    add_linkdirs("vendor/webview2/lib")
    add_links("WebView2Loader.dll")

    add_syslinks("user32", "shell32", "advapi32", "winmm", "dwmapi",
                 "ole32", "oleaut32", "version")

    set_runtimes(is_mode("debug") and "MTd" or "MT")

    add_defines("UNICODE", "_UNICODE", "WIN32_LEAN_AND_MEAN", "NOMINMAX")
    add_cxflags("/utf-8")

    -- GUI subsystem: no console window pops up when it runs in the background.
    add_ldflags("/subsystem:windows", {force = true})

    -- WebView2Loader.dll must sit next to the .exe at runtime.
    after_build(function (target)
        os.cp("vendor/webview2/lib/WebView2Loader.dll", target:targetdir())
    end)

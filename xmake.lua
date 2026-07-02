set_project("capitalizer")
set_xmakever("2.9.0")

add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

target("capitalizer")
    set_kind("binary")
    add_files("src/main.cpp")
    add_syslinks("user32", "shell32", "advapi32", "winmm")

    -- Statically link the CRT so the .exe is a standalone background utility
    -- that does not require the VC++ redistributable on the target machine.
    set_runtimes(is_mode("debug") and "MTd" or "MT")

    add_defines("UNICODE", "_UNICODE", "WIN32_LEAN_AND_MEAN", "NOMINMAX")

    -- GUI subsystem: no console window pops up when it runs in the background.
    add_ldflags("/subsystem:windows", {force = true})

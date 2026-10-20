-- Menu Search and Menu Search - Controller Keyboard: OBSE64 plugins for The Elder Scrolls IV: Oblivion Remastered. GPL-3.0-or-later.
-- /d1trimfile strips the project folder from __FILE__ and std::source_location (no build-machine paths in the DLLs); /PDBALTPATH keeps only the
-- PDB's file name in the debug directory. Both are set BEFORE includes() so CommonLibOB64's own library target gets them too.
add_cxflags({"/d1trimfile:$(projectdir)"}, {force = true, expand = false})
add_shflags("/PDBALTPATH:%_PDB%", {force = true})

includes("lib/commonlibob64")

set_project("MenuSearch")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")

target("MenuSearch")
    set_version("1.3.0")
    add_rules("commonlibob64.plugin", {
        name = "MenuSearch",
        author = "cfa911",
        description = "Search bar, typing and list filter for the Inventory, Magic and container / barter menus (Oblivion Remastered)"
    })
    add_syslinks("user32", "imm32")
    add_files("menusearch/*.cpp", "src/Ui.cpp", "src/Reflect.cpp", "src/PEHook.cpp", "src/Menus.cpp", "src/UiText.cpp")
    add_headerfiles("menusearch/*.h")
    add_includedirs("src", "menusearch")
    set_pcxxheader("src/pch.h")

-- Menu Search - Controller Keyboard: a controller on-screen keyboard for the search of the Menu Search plugin, which it needs.
target("MenuSearchControllerKeyboard")
    set_version("1.3.0")
    add_rules("commonlibob64.plugin", {
        name = "MenuSearchControllerKeyboard",
        author = "cfa911",
        description = "Controller on-screen keyboard for the Menu Search plugin (Oblivion Remastered)"
    })
    add_syslinks("user32")
    add_files("searchkb/*.cpp", "src/Ui.cpp", "src/Reflect.cpp", "src/PEHook.cpp", "src/Menus.cpp", "src/UiText.cpp")
    add_headerfiles("searchkb/*.h")
    add_includedirs("src", "searchkb")
    set_pcxxheader("src/pch.h")

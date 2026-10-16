# Search Bar for Inventory and Magic Menus — Oblivion Remastered

Two OBSE64 plugins for *The Elder Scrolls IV: Oblivion Remastered*. Both are open source (GPL-3.0-or-later). Current mod release: **v1.0.0**.

| Plugin | What it does | Needs |
|---|---|---|
| **Oblivion Menu Search** | A search bar at the top of the Inventory and Magic menus. Type part of a name and the list shows only the matches. | OBSE64, Address Library |
| **Oblivion Search Keyboard** | A controller on-screen keyboard (parchment page, the game's font and button icons) that types into that search bar. | The above, and a controller |

The two are separate downloads. The keyboard is useless without Menu Search and says so in its log. Menu Search works on its own with keyboard and mouse.

## Features

- The list stays filtered through everything the game does to it (equip, use, drop, change tab, reopen the menu). The filter runs in the same frame the game rebuilds the list, so the unfiltered list never flashes on screen.
- Each menu keeps its own search text until the game closes, and can be switched on or off in the ini.
- Keyboard and mouse: **Ctrl+F** or **Ctrl+K**, or click the bar, to type. Backspace, Ctrl+V to paste, Enter or Esc to stop.
- Controller: hold **D-pad right** for 0.4 s to open the keyboard. A types, X deletes (hold to clear), Y is space, B or Start closes. The menu stands still while it is open and the list filters as you type. A tap on D-pad right still steps the sort as usual.
- The bar's and the keyboard's look are plain PNGs (`SearchBar.png`, `Parchment.png`) that you can replace; position, size and font size are in the inis.
- Works with Improved Wheel Menu (it steps aside while the keyboard is open).

Limits: Inventory and Magic menus only (no containers, barter or shops yet); plain ASCII typing; Xbox button icons only. Built and tested on game build **1.512.105.0**.

## Install

Download the two zips from the Releases page and install them with a mod manager, or unzip into the game folder so that the files end up in `OblivionRemastered\Binaries\Win64\OBSE\Plugins\`. Requires [OBSE64](https://www.nexusmods.com/oblivionremastered/mods/282) and [Address Library for OBSE Plugins](https://www.nexusmods.com/oblivionremastered/mods/4475). UE4SS is not needed.

**Incompatible** with Inventory Search (Nexus 5395): Menu Search replaces it. If its DLL is present it logs a warning and switches itself off.

Logs: `Documents\My Games\Oblivion Remastered\OBSE\Logs\OblivionMenuSearch.log` and `OblivionSearchKeyboard.log`.

## Build

Needs Visual Studio 2022 Build Tools (MSVC v143 and the Windows SDK) and [xmake](https://xmake.io). From a Visual Studio x64 developer prompt:

```
git clone --recurse-submodules https://github.com/cfa911/Oblivion-Remastered---Mod---Search-Bar-for-Inventory-and-Magic-Menus.git
cd Oblivion-Remastered---Mod---Search-Bar-for-Inventory-and-Magic-Menus
xmake f -y -p windows -a x64 -m releasedbg
xmake -y
```

The DLLs land in `build/windows/x64/releasedbg/`. CommonLibOB64 is a submodule pinned to commit `3e5ee4f` (its `commonlib-shared` at `9fbb74d`).

## Layout

```
menusearch/   Oblivion Menu Search: bar, typing, list filter, controller tick, exports used by the keyboard
searchkb/     Oblivion Search Keyboard: on-screen keyboard, controller hook, client of the search API
src/          helpers both plugins compile: Ui, Reflect, PEHook, Menus (the game's Unreal objects), UiText (text and font)
tools/        gen_searchbar.py, gen_parchment.py (make the PNGs), merge_ini.py
```

The keyboard finds `OblivionMenuSearch.dll` by name and calls its exports: `OblivionMenuSearch_ApiVersion`, `_Enabled`, `_GetText`, `_SetText`, `_SetExternalInput`. The keyboard exports `OblivionSearchKeyboard_IsOpen` for other plugins that want to stand aside.

## Licence and credits

GPL-3.0-or-later, see [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md). Third-party notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

- Helper files `Ui`, `Reflect`, `PEHook` and `Menus` are adapted from Improved Wheel Menu by ApocryphaRealm (GPL-3.0).
- Built on [CommonLibOB64](https://github.com/libxse/commonlibob64) (GPL-3.0) and spdlog (MIT).

# Menu Search — search bars for the Inventory, Magic, Quests and Container menus (Oblivion Remastered)

Two OBSE64 plugins for *The Elder Scrolls IV: Oblivion Remastered*. Both are open source (GPL-3.0-or-later). Current mod release: **v1.2.0**.

| Plugin | What it does | Needs |
|---|---|---|
| **Menu Search** (`MenuSearch.dll`) | A search bar in the Inventory, Magic and quests menus, and one on each side of the container / barter menu. Type part of a name and the list shows only the matches. | OBSE64, Address Library |
| **Menu Search - Controller Keyboard** (`MenuSearchControllerKeyboard.dll`) | A controller on-screen keyboard (parchment page, the game's font and button icons) that types into those bars. | Menu Search, and a controller |

The two are separate downloads. The keyboard is useless without Menu Search and says so in its log. Menu Search works on its own with keyboard and mouse. (Both were called *Oblivion Menu Search* and *Oblivion Search Keyboard* up to 1.1.0; uninstall those before installing these.)

## Features

- The list stays filtered through everything the game does to it (equip, use, drop, buy, sell, change tab, reopen the menu). The filter runs in the same frame the game rebuilds the list, so the unfiltered list never flashes on screen.
- **Inventory, Magic and quests:** one bar at the top of the menu.
- **Container and barter menu:** a green bar over your list (*Player*) and a red one over the container's or the merchant's (*Container* / *Seller*). Each side is searched on its own. A plain container shows only its own bar.
- Keyboard and mouse: **Ctrl+F**, or click a bar, to type. Backspace, Ctrl+V to paste, Ctrl+A to select all, Enter or Esc to stop. In the container menu Ctrl+F types into the side the mouse is on.
- Controller: hold **D-pad right** for 0.4 s to open the keyboard (in the container menu: right = your list, **D-pad left** = the other one; it opens on the other half of the screen so it never covers the list you search). A types, X deletes (hold to clear), Y is space, B or Start closes. The first letter of a search is a capital. The menu stands still while it is open, but LT and RT still switch its tabs and the search follows the list. A tap on D-pad left or right still does what the game does.
- Every bar's picture, place and size are in the inis; the pictures are plain PNGs you can replace.
- Works with Improved Wheel - Keyboard and Better Controller Support 1.1.2 or newer (it steps aside while the keyboard is open).

Limits: the Inventory, Magic, quests and container / barter menus (spell merchants' spell lists are not searched; a body's bar says *Container*); plain ASCII typing; Xbox button icons only. Built and tested on game build **1.512.105.0**.

## Install

Download the two zips from the Releases page and install them with a mod manager, or unzip into the game folder so that the files end up in `OblivionRemastered\Binaries\Win64\OBSE\Plugins\`. Requires [OBSE64](https://www.nexusmods.com/oblivionremastered/mods/282) and [Address Library for OBSE Plugins](https://www.nexusmods.com/oblivionremastered/mods/4475). UE4SS is not needed.


Logs: `Documents\My Games\Oblivion Remastered\OBSE\Logs\MenuSearch.log` and `MenuSearchControllerKeyboard.log`.

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
menusearch/   Menu Search: bars, typing, list filter, the container / barter menu (Container.cpp), the quests tab (Quests.cpp), controller tick, exports used by the keyboard
searchkb/     Menu Search - Controller Keyboard: on-screen keyboard, controller hook, client of the search API
src/          helpers both plugins compile: Ui, Reflect, PEHook, Menus (the game's Unreal objects), UiText (text and font)
tools/        gen_searchbar_side.py, gen_parchment.py (make the PNGs), merge_ini.py
```

The keyboard finds `MenuSearch.dll` by name (`OblivionMenuSearch.dll` for 1.x of the plugin) and calls its exports: `MenuSearch_ApiVersion` (2), `_Enabled`, `_GetText`, `_SetText`, `_SetExternalInput`, and since version 2 `_ActiveContext` and `_PanelOnLeft`. Menu ids: 1 Inventory, 2 Magic, 3 the container menu's your side, 4 its other side, 5 the quests tab. The keyboard exports `MenuSearchControllerKeyboard_IsOpen` for other plugins that want to stand aside.

## Licence and credits

GPL-3.0-or-later, see [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md). Third-party notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

- Helper files `Ui`, `Reflect`, `PEHook` and `Menus` are adapted from Improved Wheel Menu by ApocryphaRealm (GPL-3.0).
- Built on [CommonLibOB64](https://github.com/libxse/commonlibob64) (GPL-3.0) and spdlog (MIT).

# Changelog

## 1.2.1

### Changed
- **New names:** *Menu Search* is now **SearchLight** and *Menu Search - Controller Keyboard* is **SearchLight - Controller Keyboard**. The files keep their names (`MenuSearch.dll`, `MenuSearch.ini`, `MenuSearchControllerKeyboard.dll` ...), so settings and the plugins' lookups by name are untouched; the release archives are named `SearchLightv1.2.1.zip`, `SearchLightControllerKeyboardv1.2.1.zip` and `SearchLightSourcev1.2.1.zip`.
- SearchLight no longer stands down when Inventory Search or Magic Search is installed (the check and the README line are gone). It still stands down for its own old name, Oblivion Menu Search 1.x.

## 1.2.0

### Added
- **Quests tab:** a search bar (where the Magic menu's sits; `[Quest]` in the ini moves it) that filters the quest list, with the controller keyboard too (`bQuests` switches it off).
- **Ctrl+A** selects all the text of a bar: the next key replaces it, Backspace or Delete clears it; it is drawn in the bar's gold.
- The controller keyboard capitalizes the first letter of a search, and lets **LT and RT** through, so they still switch the menu's tabs while you type.
- New `SearchBar.png`: black like before, now with the jagged golden outline of the container bars (`tools/gen_searchbar_side.py black 972 72`).

### Changed
- **Ctrl+F is the only shortcut** that starts typing (Ctrl+K is gone), in every menu.
- **New names:** the mods are *Menu Search* and *Menu Search - Controller Keyboard*; the files are `MenuSearch.dll` / `MenuSearch.ini` / `MenuSearch\` and `MenuSearchControllerKeyboard.dll` / `.ini` / folder, the logs `MenuSearch.log` and `MenuSearchControllerKeyboard.log`, the API exports `MenuSearch_*`. The keyboard still finds a 1.x `OblivionMenuSearch.dll`. Uninstall the 1.x files first; if both are installed the new plugins stand down.
- Menu Search API: menu id 5 is the quests tab (`OblivionMenuSearch_ActiveContext` / `MenuSearch_ActiveContext` returns 5 there).

## 1.1.0

### Added
- **Container and barter menu:** a search bar on each side — a green one over your list (*Player*) and a red one over the container's or the merchant's (*Container* / *Seller*) — each with its own search text and the same flash-free filter. A plain container shows only its own bar; no bar is shown for a side that is not on the screen. The bars follow the layout (a plain container, a transfer, a barter, an item's details beside the list).
- Every bar has its own place and size in the ini: `[Inventory]`, `[Magic]`, `[ContainerYours]`, `[ContainerTheirs]`, `[ContainerTake]`; `[Search] bContainer` switches the container menu on or off. The 1.0.0 `[Bar]` keys still work as the defaults of `[Inventory]` and `[Magic]`.
- New pictures `SearchBarYours.png` (green) and `SearchBarTheirs.png` (red), with a jagged golden band; `tools/gen_searchbar_side.py` makes them.
- Controller keyboard in the container / barter menu: hold D-pad right for your list, D-pad left for the other (`uButtonLeft` in the keyboard's ini); either button in a plain container. The keyboard opens on the other half of the screen from the list it types into.
- Menu Search API version 2: `OblivionMenuSearch_ActiveContext`, `OblivionMenuSearch_PanelOnLeft`, menu ids 3 and 4. The keyboard still works with version 1 (Inventory and Magic only).

### Changed
- The keyboard package is called **Menu Search - Controller Keyboard** (it was Oblivion Search Keyboard). The plugin file is still `OblivionSearchKeyboard.dll`.

## 1.0.0

First release: the search bar and filter for the Inventory and Magic menus, and the controller keyboard.

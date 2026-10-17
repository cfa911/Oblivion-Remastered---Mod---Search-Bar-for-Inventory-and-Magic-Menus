# Changelog

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

#pragma once

// The Controller Keyboard types into the search of the Menu Search plugin (MenuSearch.dll): its text, per menu. The functions are
// found by name when needed; without that plugin (or with the search off in a menu) the keyboard has nothing to type into and stays off.

#include "Menus.h"

namespace search
{
	bool        Present();                    // MenuSearch.dll is loaded and has the API
	bool        Enabled(menus::Menu a_menu);  // ... and its search is on for this menu
	std::string GetText(menus::Menu a_menu);
	void        SetText(menus::Menu a_menu, const std::string& a_text);
	void        SetExternalInput(bool a_open);
	// the menu the keyboard works in: the Inventory, the Magic menu, the quests tab (kQuests), or kContainerYours standing for the container / barter menu (its two sides
	// are kContainerYours and kContainerTheirs); kNone elsewhere. Needs the Menu Search API version 2 for the container menu.
	menus::Menu Context();
	// a container / barter side's list is on the left half of the screen (the keyboard then opens on the right); the Inventory and Magic menus: true
	bool        PanelOnLeft(menus::Menu a_menu);   // the keyboard has the text: the search bar stops taking typed keys
}

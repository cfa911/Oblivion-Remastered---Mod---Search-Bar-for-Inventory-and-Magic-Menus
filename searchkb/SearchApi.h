#pragma once

// The Search Keyboard types into the search of the Oblivion Menu Search plugin (OblivionMenuSearch.dll): its text, per menu. The functions are
// found by name when needed; without that plugin (or with the search off in a menu) the keyboard has nothing to type into and stays off.

#include "Menus.h"

namespace search
{
	bool        Present();                    // OblivionMenuSearch.dll is loaded and has the API
	bool        Enabled(menus::Menu a_menu);  // ... and its search is on for this menu
	std::string GetText(menus::Menu a_menu);
	void        SetText(menus::Menu a_menu, const std::string& a_text);
	void        SetExternalInput(bool a_open);   // the keyboard has the text: the search bar stops taking typed keys
}

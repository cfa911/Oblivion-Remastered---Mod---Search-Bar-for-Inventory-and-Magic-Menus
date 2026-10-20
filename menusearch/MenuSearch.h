#pragma once

// ============================================================================================================
// Menu Search: a search bar, typing for it and a list filter for the Inventory and Magic menus (Oblivion Remastered).
// Game thread unless said otherwise.
// ============================================================================================================

#include "Menus.h"

namespace msearch
{
	void Tick(menus::Menu a_menu);   // once per frame (the controller read, and a timer on the game window); does the work

	void SetGameThread(DWORD a_id);   // the thread the engine's objects are used on (the one the plugin was loaded on)
	void SetConflict(bool a_conflict);   // another search mod is installed: everything stands down

	// ---- what the Controller Keyboard plugin uses (through the exports in main.cpp); any thread ----
	bool        Enabled(menus::Menu a_menu);   // the search is on for this menu
	std::string GetText(menus::Menu a_menu);
	void        SetText(menus::Menu a_menu, const std::string& a_text);
	bool        PanelOnLeft(menus::Menu a_menu);   // the container / barter side's list is on the left half of the screen (the keyboard goes on the other half)
	int         Context();   // the open menu for the keyboard: 0 none, 1 Inventory, 2 Magic, 3 the container / barter menu (both sides: ids 3 and 4)
	bool        CursorOverBar();   // the mouse is over a bar that is shown (a click there is for the bar; the Improved Wheels Plus plugin leaves it alone)
	bool        Typing();   // keys are being typed into one of the bars right now (the Improved Wheels Plus plugin leaves its digit keys alone then)
	void        SetExternalInput(bool a_open);   // the controller keyboard has the text: typed keys are not taken
}

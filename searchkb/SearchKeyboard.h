#pragma once

// ============================================================================================================
// A controller on-screen keyboard for the search of the Inventory and Magic menus (the Oblivion Menu Search plugin). A hold on a button
// (D-pad right) opens it on the right of the screen; while it is open it takes the whole controller read (the game sees an idle pad, so the
// menu stands still) and the search text is set through the Menu Search plugin, which filters as it always does.
//   D-pad / left stick: move   A: type   X: delete (hold: clear)   Y: space   B / Start: close (the search stays)
// Game thread only (it runs inside the controller read, XInputHook.cpp).
// ============================================================================================================

#include "Menus.h"

namespace osk
{
	bool Active();
	bool Taking();    // the on-screen keyboard is up: the pad belongs to it

	// the keyboard can open in this menu (the Menu Search plugin is there and has the search on for it)
	bool CanOpen(menus::Menu a_menu);

	// a_held: the buttons down at this moment (they are ignored until let go). False when it could not open.
	bool Open(menus::Menu a_menu, std::uint16_t a_held);

	// one controller read while it is open: a_raw the buttons, the left stick, now
	void Rewrite(std::uint16_t a_raw, std::int16_t a_lx, std::int16_t a_ly, std::chrono::steady_clock::time_point a_now);

	void Close(const char* a_why, std::uint16_t a_held = 0);

	// the buttons the game must still not see after it closed: those held when it did, until each is let go
	std::uint16_t Swallow(std::uint16_t a_raw);
}

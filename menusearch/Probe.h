#pragma once

// A log-only look at the menus the search does not cover yet (barter, containers): while a menu other than the Inventory or the Magic
// menu is open, which of the game's widgets that look like part of it are alive and on screen. Off unless [Debug] bProbe=1.

namespace probe
{
	void Tick();   // the game thread, from the plugin's tick
}

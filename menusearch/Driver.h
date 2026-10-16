#pragma once

// The per-frame tick of the search: the game polls the controller through its own XINPUT1_3!XInputGetState import every frame; that import slot is
// chained here (the previous target is kept and called - this never changes what the game reads). Every read of controller 0 on the game thread
// runs msearch::Tick. A second source of ticks, for a player without a controller, is a timer on the game window (MenuSearch.cpp).

namespace driver
{
	bool Install();   // once, at OBSE's post-load, on the game's thread
}

#pragma once

// ============================================================================================================
// The controller read. The game polls the pad through its own XINPUT1_3!XInputGetState import; that import slot is chained
// here (the previous target is kept and called, so any other plugin that chained it first - Steam Input's, the Improved
// Wheel Menu's - keeps working underneath). Every read of controller 0 on the game thread passes through Rewrite, which
// times the keyboard's button as tap or hold and, while the keyboard is open, hands the game an idle pad.
// Never loads an XInput DLL: only the game's own import is used.
// ============================================================================================================

namespace hook
{
	bool Install();   // call once at OBSE's post-load, on the game's main thread (idempotent)
}

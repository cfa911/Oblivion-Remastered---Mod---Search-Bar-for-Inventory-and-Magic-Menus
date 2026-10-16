#pragma once

// OblivionSearchKeyboard.ini beside the plugin. Read once at load; missing file or keys keep the defaults below.

namespace cfg
{
	struct Values
	{
		int  logLevel = 2;      // [Log] uLogLevel: 0 trace, 1 debug, 2 info, 3 warn, 4 error

		// [SearchKeyboard]
		bool enabled = true;    // bEnabled
		int  button = 0x0008;   // uButton: XInput mask held to open it (8 = D-pad right; a tap stays the game's own)
		int  holdMs = 400;      // uHoldMs: how long the button is held before the keyboard opens
		int  leftPercent = 49, topPercent = 14, rightPercent = 93, bottomPercent = 87;   // where it sits, as a share of the screen
		int  textPercent = 100; // uTextPercent: the keys' text size
	};

	void          Load();
	std::filesystem::path PluginFolder();   // the folder the DLL is in (OBSE\Plugins)
	const Values& Get();
}

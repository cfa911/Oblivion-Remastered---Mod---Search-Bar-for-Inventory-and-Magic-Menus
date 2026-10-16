#pragma once

// OblivionMenuSearch.ini beside the plugin. Read once at load; missing file or keys keep the defaults below.

namespace cfg
{
	struct Values
	{
		int  logLevel = 2;      // [Log] uLogLevel: 0 trace, 1 debug, 2 info, 3 warn, 4 error

		bool inventory = true;  // [Search] bInventory: the Inventory menu has the search
		bool magic = true;      // [Search] bMagic: the Magic menu has the search

		int    barFontSize = 19;   // [Bar] uFontSize
		double barX = 289, barY = 111, barW = 486, barH = 36;   // where the bar sits in the menu's root canvas (menu units), and its size
		double barYInventory = -1.5, barYMagic = -0.75;         // added to uY in that menu only (negative = up)

		bool ownFilter = true;  // [Filter] bEnabled: filter the lists (and filter again at once when the game rebuilds one)
	};

	void          Load();
	std::filesystem::path PluginFolder();   // the folder the DLL is in (OBSE\Plugins)
	const Values& Get();
}

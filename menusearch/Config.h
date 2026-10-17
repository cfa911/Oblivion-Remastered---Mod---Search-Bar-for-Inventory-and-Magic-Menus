#pragma once

// OblivionMenuSearch.ini beside the plugin. Read once at load; missing file or keys keep the defaults below.

namespace cfg
{
	struct Values
	{
		int  logLevel = 2;      // [Log] uLogLevel: 0 trace, 1 debug, 2 info, 3 warn, 4 error

		bool inventory = true;  // [Search] bInventory: the Inventory menu has the search
		bool magic = true;      // [Search] bMagic: the Magic menu has the search
		bool container = true;  // [Search] bContainer: the container / barter menu has the search (a bar on each side)

		int    barFontSize = 19;   // [Bar] uFontSize
		double barX = 289, barY = 111, barW = 486, barH = 36;   // where the bar sits in the menu's root canvas (menu units), and its size
		double barYInventory = -1.5, barYMagic = -0.75;         // added to uY in that menu only (negative = up)

		// where each bar sits and how big it is (menu units, the menu's root canvas): [Inventory] and [Magic] hold the bar's top-left corner and size;
		// [ContainerYours] and [ContainerTheirs] (the two sides of the container / barter menu) hold an offset from the place above the side's header,
		// and a size (width 0 = as wide as the header). The older [Bar] keys are the defaults of the first two.
		struct Place
		{
			double x = 0, y = 0, w = 0, h = 0;
		};
		Place inventoryBar{ 289, 109.5, 486, 36 }, magicBar{ 289, 110.25, 486, 36 };
		Place yoursBar{ -48, -90, 825, 37 }, theirsBar{ -48, -90, 823, 37 };
				Place takeBar{ -48, -11, 0, 37 };   // the container's own list alone on the screen (nothing to give): width 0 = the header's plus the offset on both sides

		bool probe = false;     // [Debug] bProbe: log which barter / container widgets are alive and on screen (diagnostic only)

		bool ownFilter = true;  // [Filter] bEnabled: filter the lists (and filter again at once when the game rebuilds one)
	};

	void          Load();
	std::filesystem::path PluginFolder();   // the folder the DLL is in (OBSE\Plugins)
	const Values& Get();
}

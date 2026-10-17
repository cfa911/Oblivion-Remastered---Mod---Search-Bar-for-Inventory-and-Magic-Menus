#pragma once

// ============================================================================================================
// The container / barter menu (Oblivion Remastered): one widget, WBP_ModernMenu_Container_C, for loot, containers and merchants' barter
// (read in game 2026-10-17). Two sides, each a WBP_ModernMenu_ContainerInventory_MainContent_C with its own header and list:
// cont_give_maincontent = yours, cont_take_maincontent = the container's or the merchant's. Found by the widget's activation, as menus.cpp
// does for the Inventory and the Magic menu; Menus.cpp is shared with the wheel plugin and stays as it was.
// ============================================================================================================

#include "Menus.h"

namespace container
{
	void         Tick();      // the game thread, a few times a second: finds the class once it has loaded and watches its activation
	bool         Open();      // the container / barter menu is the open menu
	UE::UClass*  Class();
	UE::UObject* Widget();    // the menu widget (nullptr when there is none)
	// one side's main content widget (it holds the side's header, tabs and list); a_side is kContainerYours or kContainerTheirs
	UE::UObject* SideContent(menus::Menu a_side);
}

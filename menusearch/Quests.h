#pragma once

// ============================================================================================================
// The quests tab (Oblivion Remastered's journal): WBP_Modern_QuestPage_C holds a list panel (WBP_Modern_QuestListPanel_C) with the quest list
// (WBP_Modern_QuestListView_C), a detail panel and a history panel (read from the game's asset index 2026-10-17). The page is "open" while a
// shown instance of it is on the screen; the menu's list is found inside it the way the Inventory's is.
// ============================================================================================================

#include "Menus.h"

namespace quests
{
	void         Tick();       // the game thread, a few times a second: finds the classes once they are loaded
	bool         Open();       // the quests page is on the screen (cheap: looked at about every 150 ms)
	UE::UClass*  PageClass();
	UE::UClass*  PanelClass();
	void         NoteInstance(UE::UObject* a_page);   // the engine just called this page widget (from the post-call hook): a candidate for the shown page
	UE::UObject* Page();       // the shown page widget, nullptr when there is none
	UE::UObject* ListPanel();  // the shown list panel inside it
	UE::UObject* ListWrapper();   // the shown quest list view inside it
}

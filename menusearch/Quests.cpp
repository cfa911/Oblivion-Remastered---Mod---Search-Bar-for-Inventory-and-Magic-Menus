#include "Quests.h"

#include "Reflect.h"
#include "Ui.h"

namespace quests
{
	namespace
	{
		constexpr const wchar_t* kPagePath = L"/Game/UI/Original/GameMenuLayer/Quest/WBP_Modern_QuestPage.WBP_Modern_QuestPage_C";
		constexpr const wchar_t* kPanelPath = L"/Game/UI/Original/GameMenuLayer/Quest/WBP_Modern_QuestListPanel.WBP_Modern_QuestListPanel_C";
		constexpr const wchar_t* kListPath = L"/Game/UI/Original/GameMenuLayer/Quest/Prefabs/WBP_Modern_QuestListView.WBP_Modern_QuestListView_C";

		UE::UClass*     g_page = nullptr;
		UE::UClass*     g_panel = nullptr;
		UE::UClass*     g_list = nullptr;
		reflect::Handle g_pageW, g_hookedW, g_panelW, g_listW;   // kept between looks: the game keeps these widgets between visits
		ULONGLONG       g_next = 0, g_scanPage = 0, g_scanPanel = 0, g_scanList = 0;
		bool            g_open = false;

		// the shown instance of a class (inside a_within when given): the kept one while it is shown, else one found by walking the object table -
		// at most once per a_interval, so nothing is walked while nothing is wanted
		UE::UObject* ShownKept(reflect::Handle& a_handle, UE::UClass* a_class, UE::UObject* a_within, ULONGLONG& a_nextScan, ULONGLONG a_interval)
		{
			if (auto* o = reflect::Get(a_handle); o && (!a_within || ui::IsWithin(o, a_within)) && ui::ShownOnScreen(o)) return o;
			a_handle = {};
			const ULONGLONG now = GetTickCount64();
			if (!a_class || now < a_nextScan) return nullptr;
			a_nextScan = now + a_interval;
			for (auto* o : reflect::Instances(a_class)) {
				if (a_within && !ui::IsWithin(o, a_within)) continue;
				if (ui::ShownOnScreen(o)) {
					a_handle = reflect::Hold(o);
					return o;
				}
			}
			return nullptr;
		}
	}

	void Tick()
	{
		if (!reflect::Ok()) return;
		if (!g_page) g_page = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kPagePath);
		if (!g_panel) g_panel = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kPanelPath);
		if (!g_list) g_list = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kListPath);
	}

	void NoteInstance(UE::UObject* a_page)
	{
		if (a_page) g_hookedW = reflect::Hold(a_page);
	}

	bool Open()
	{
		if (!g_page || !menus::AnyOpen()) {
			g_open = false;
			return false;
		}
		const ULONGLONG now = GetTickCount64();
		if (now >= g_next) {
			g_next = now + 150;
			bool shown = false;
			if (auto* w = reflect::Get(g_pageW); w && ui::ShownOnScreen(w)) {
				shown = true;
			} else if (auto* h = reflect::Get(g_hookedW); h && ui::ShownOnScreen(h)) {   // the page the engine last called: no object table walk needed
				g_pageW = g_hookedW;
				shown = true;
			} else {
				shown = ShownKept(g_pageW, g_page, nullptr, g_scanPage, 2000) != nullptr;   // the fallback: a walk, at most every 2 s, only while a menu is open
			}
			if (shown != g_open) logger::debug("quests: the quests page is {}", shown ? "on the screen" : "gone");
			g_open = shown;
		}
		return g_open;
	}

	UE::UClass* PageClass() { return g_page; }
	UE::UClass* PanelClass() { return g_panel; }

	UE::UObject* Page() { return Open() ? reflect::Get(g_pageW) : nullptr; }

	UE::UObject* ListPanel()
	{
		auto* page = Page();
		return page ? ShownKept(g_panelW, g_panel, page, g_scanPanel, 500) : nullptr;
	}

	UE::UObject* ListWrapper()
	{
		auto* page = Page();
		return page ? ShownKept(g_listW, g_list, page, g_scanList, 500) : nullptr;
	}
}

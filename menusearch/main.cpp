// Menu Search (Oblivion Remastered) - entry point.
// A search bar, typing for it and a list filter for the Inventory and Magic menus.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"
#include "Driver.h"
#include "MenuSearch.h"

namespace
{
	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != OBSE::MessagingInterface::kPostLoad) {
			return;
		}
		msearch::SetGameThread(GetCurrentThreadId());   // OBSE's post-load runs on the thread the engine's objects are used on
		// The old file name of this plugin would give two search bars and two filters on one list: this plugin stands down and says so.
		if (GetModuleHandleW(L"OblivionMenuSearch.dll")) {   // Oblivion Menu Search 1.x (this plugin's old file name) is installed too
			logger::warn("Oblivion Menu Search 1.x (OblivionMenuSearch.dll) is installed - this plugin is the same mod under a new name, so it stands down. Uninstall the old one.");
			msearch::SetConflict(true);
			return;
		}
		driver::Install();
	}
}

// What the Controller Keyboard plugin (MenuSearchControllerKeyboard.dll) uses, by name through GetProcAddress.A menu is 1 = Inventory, 2 = Magic, 3 = the container / barter menu's your side, 4 = its other side.
extern "C"
{
	__declspec(dllexport) int MenuSearch_ApiVersion()
	{
		return 2;   // 2 added the container / barter menu: menus 3 (yours) and 4 (the other side), and MenuSearch_ActiveContext
	}

	// the menu that is open: 0 none, 1 Inventory, 2 Magic, 3 the container / barter menu (its sides are the menu ids 3 and 4)
	__declspec(dllexport) int MenuSearch_ActiveContext()
	{
		return msearch::Context();
	}

	// a container / barter side's list is on the left half of the screen (the keyboard goes on the other half); version 2
	__declspec(dllexport) bool MenuSearch_PanelOnLeft(int a_menu)
	{
		return msearch::PanelOnLeft(static_cast<menus::Menu>(a_menu));
	}

	__declspec(dllexport) bool MenuSearch_Enabled(int a_menu)
	{
		return msearch::Enabled(static_cast<menus::Menu>(a_menu));
	}

	__declspec(dllexport) int MenuSearch_GetText(int a_menu, char* a_buf, int a_cap)
	{
		const auto s = msearch::GetText(static_cast<menus::Menu>(a_menu));
		if (a_buf && a_cap > 0) {
			const int n = std::min(static_cast<int>(s.size()), a_cap - 1);
			std::memcpy(a_buf, s.data(), static_cast<std::size_t>(n));
			a_buf[n] = '\0';
		}
		return static_cast<int>(s.size());
	}

	__declspec(dllexport) void MenuSearch_SetText(int a_menu, const char* a_text)
	{
		msearch::SetText(static_cast<menus::Menu>(a_menu), a_text ? a_text : "");
	}

	// a bar has the typing (the game window's keys go to it): the Improved Wheels Plus plugin's digit keys are not slot keys then; any thread
	__declspec(dllexport) bool MenuSearch_IsTyping()
	{
		return msearch::Typing();
	}

	// the mouse is over a bar that is shown: a click there starts the typing, the Improved Wheels Plus plugin does not read it as a click on its wheel; any thread
	__declspec(dllexport) bool MenuSearch_CursorOverBar()
	{
		return msearch::CursorOverBar();
	}

	__declspec(dllexport) void MenuSearch_SetExternalInput(bool a_open)
	{
		msearch::SetExternalInput(a_open);
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	cfg::Load();
	{
		const auto level = static_cast<spdlog::level::level_enum>(std::clamp(cfg::Get().logLevel, 0, 4));
		logger::set_level(level, level);
	}
	logger::info("Menu Search loaded");
	if (auto* messaging = OBSE::GetMessagingInterface()) {
		if (!messaging->RegisterListener(&OnMessage)) {
			logger::error("OBSE messaging: listener refused - the search will not start");
		}
	} else {
		logger::error("OBSE messaging interface missing - the search will not start");
	}
	return true;
}

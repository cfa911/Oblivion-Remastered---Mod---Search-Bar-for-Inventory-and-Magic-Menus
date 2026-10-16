// Oblivion Menu Search (Oblivion Remastered) - entry point.
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
		// Another search mod would give two search bars and two filters on one list: this plugin stands down and says so.
		const bool magic = GetModuleHandleW(L"OblivionMagicSearch.dll") != nullptr;
		const bool inventory = GetModuleHandleW(L"OblivionInventorySearch.dll") != nullptr;
		if (magic || inventory) {
			logger::warn("{}{}{} is installed - this plugin does its own search, so it stands down. Disable that mod to use this one.",
				magic ? "Magic Search (OblivionMagicSearch.dll)" : "", magic && inventory ? " and " : "", inventory ? "Inventory Search (OblivionInventorySearch.dll)" : "");
			msearch::SetConflict(true);
			return;
		}
		driver::Install();
	}
}

// What the Search Keyboard plugin (OblivionSearchKeyboard.dll) uses, by name through GetProcAddress. A menu is 1 = Inventory, 2 = Magic.
extern "C"
{
	__declspec(dllexport) int OblivionMenuSearch_ApiVersion()
	{
		return 1;
	}

	__declspec(dllexport) bool OblivionMenuSearch_Enabled(int a_menu)
	{
		return msearch::Enabled(static_cast<menus::Menu>(a_menu));
	}

	__declspec(dllexport) int OblivionMenuSearch_GetText(int a_menu, char* a_buf, int a_cap)
	{
		const auto s = msearch::GetText(static_cast<menus::Menu>(a_menu));
		if (a_buf && a_cap > 0) {
			const int n = std::min(static_cast<int>(s.size()), a_cap - 1);
			std::memcpy(a_buf, s.data(), static_cast<std::size_t>(n));
			a_buf[n] = '\0';
		}
		return static_cast<int>(s.size());
	}

	__declspec(dllexport) void OblivionMenuSearch_SetText(int a_menu, const char* a_text)
	{
		msearch::SetText(static_cast<menus::Menu>(a_menu), a_text ? a_text : "");
	}

	__declspec(dllexport) void OblivionMenuSearch_SetExternalInput(bool a_open)
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
	logger::info("Oblivion Menu Search loaded");
	if (auto* messaging = OBSE::GetMessagingInterface()) {
		if (!messaging->RegisterListener(&OnMessage)) {
			logger::error("OBSE messaging: listener refused - the search will not start");
		}
	} else {
		logger::error("OBSE messaging interface missing - the search will not start");
	}
	return true;
}

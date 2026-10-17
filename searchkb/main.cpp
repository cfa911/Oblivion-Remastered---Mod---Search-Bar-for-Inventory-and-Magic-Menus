// Menu Search - Controller Keyboard (MenuSearchControllerKeyboard.dll, Oblivion Remastered) - entry point.
// A controller on-screen keyboard for the search of the Oblivion Menu Search plugin (it needs that plugin).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"
#include "SearchApi.h"
#include "SearchKeyboard.h"
#include "XInputHook.h"

namespace
{
	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != OBSE::MessagingInterface::kPostLoad) {
			return;
		}
		if (GetModuleHandleW(L"OblivionSearchKeyboard.dll")) {   // Oblivion Search Keyboard 1.x (this plugin's old file name) is installed too: two keyboards would both take the pad
			logger::warn("Oblivion Search Keyboard 1.x (OblivionSearchKeyboard.dll) is installed - this plugin is the same mod under a new name, so it stands down. Uninstall the old one.");
			return;
		}
		if (!search::Present()) {
			logger::warn("MenuSearch.dll (Menu Search) is not installed - this keyboard types into its search bar, so it stays off until that plugin is there.");
		}
		hook::Install();
	}
}

// Asked by the Improved Wheel (by name, through GetProcAddress): while the on-screen keyboard is up it has the controller, and the
// wheel plugin's menu rules (X on a favourite is not a drop, Select favourites...) stand aside.
extern "C" __declspec(dllexport) bool MenuSearchControllerKeyboard_IsOpen()
{
	return osk::Taking();
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	cfg::Load();
	{
		const auto level = static_cast<spdlog::level::level_enum>(std::clamp(cfg::Get().logLevel, 0, 4));
		logger::set_level(level, level);
	}
	logger::info("Menu Search - Controller Keyboard loaded");
	if (auto* messaging = OBSE::GetMessagingInterface()) {
		if (!messaging->RegisterListener(&OnMessage)) {
			logger::error("OBSE messaging: listener refused - the controller hook will not be installed");
		}
	} else {
		logger::error("OBSE messaging interface missing - the controller hook will not be installed");
	}
	return true;
}

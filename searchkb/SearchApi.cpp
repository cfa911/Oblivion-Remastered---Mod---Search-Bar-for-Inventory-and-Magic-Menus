#include "SearchApi.h"

namespace search
{
	namespace
	{
		struct Api
		{
			int (*version)() = nullptr;
			bool (*enabled)(int) = nullptr;
			int (*getText)(int, char*, int) = nullptr;
			void (*setText)(int, const char*) = nullptr;
			void (*external)(bool) = nullptr;
			int (*context)() = nullptr;   // version 2
			bool (*panelOnLeft)(int) = nullptr;   // version 2
		};
		Api g_api;

		bool Resolve()
		{
			static ULONGLONG s_next = 0;
			if (g_api.enabled) return true;
			const ULONGLONG now = GetTickCount64();
			if (now < s_next) return false;
			s_next = now + 2000;   // looked for again every two seconds until it is there
			const char* prefix = "MenuSearch_";
			HMODULE     m = GetModuleHandleW(L"MenuSearch.dll");
			if (!m) {   // Oblivion Menu Search 1.x, the plugin under its old file name
				m = GetModuleHandleW(L"OblivionMenuSearch.dll");
				prefix = "OblivionMenuSearch_";
			}
			if (!m) return false;
			Api a;
			a.version = reinterpret_cast<int (*)()>(GetProcAddress(m, (std::string(prefix) + "ApiVersion").c_str()));
			a.enabled = reinterpret_cast<bool (*)(int)>(GetProcAddress(m, (std::string(prefix) + "Enabled").c_str()));
			a.getText = reinterpret_cast<int (*)(int, char*, int)>(GetProcAddress(m, (std::string(prefix) + "GetText").c_str()));
			a.setText = reinterpret_cast<void (*)(int, const char*)>(GetProcAddress(m, (std::string(prefix) + "SetText").c_str()));
			a.external = reinterpret_cast<void (*)(bool)>(GetProcAddress(m, (std::string(prefix) + "SetExternalInput").c_str()));
			a.context = reinterpret_cast<int (*)()>(GetProcAddress(m, (std::string(prefix) + "ActiveContext").c_str()));
			const int ver = a.version ? a.version() : 0;
			a.panelOnLeft = reinterpret_cast<bool (*)(int)>(GetProcAddress(m, (std::string(prefix) + "PanelOnLeft").c_str()));
			if (ver < 2) {
				a.context = nullptr;
				a.panelOnLeft = nullptr;
			}   // version 1: the Inventory and the Magic menu only
			if (!a.version || !a.enabled || !a.getText || !a.setText || !a.external || ver < 1 || ver > 2) {
				static bool s_said = false;
				if (!s_said) {
					s_said = true;
					logger::warn("search: Menu Search is loaded but its interface is not the one this keyboard knows(version 1 or 2) - the keyboard stays off");
				}
				return false;
			}
			g_api = a;
			logger::info("search: the Menu Search plugin is there - the keyboard types into its search bar");
			return true;
		}
	}

	bool Present()
	{
		return Resolve();
	}

	bool Enabled(menus::Menu a_menu)
	{
		return Resolve() && g_api.enabled(static_cast<int>(a_menu));
	}

	std::string GetText(menus::Menu a_menu)
	{
		if (!Resolve()) return {};
		char buf[128]{};
		g_api.getText(static_cast<int>(a_menu), buf, static_cast<int>(sizeof(buf)));
		return buf;
	}

	void SetText(menus::Menu a_menu, const std::string& a_text)
	{
		if (Resolve()) g_api.setText(static_cast<int>(a_menu), a_text.c_str());
	}

	bool PanelOnLeft(menus::Menu a_menu)
	{
		if (Resolve() && g_api.panelOnLeft) return g_api.panelOnLeft(static_cast<int>(a_menu));
		return a_menu != menus::Menu::kContainerTheirs;
	}

	menus::Menu Context()
{
		if (Resolve() && g_api.context) {
			const int c = g_api.context();
			return c == 3 ? menus::Menu::kContainerYours : c == 1 ? menus::Menu::kInventory : c == 2 ? menus::Menu::kMagic : c == 5 ? menus::Menu::kQuests : menus::Menu::kNone;
		}
		return menus::Active();
	}

	void SetExternalInput(bool a_open)
{
		if (Resolve()) g_api.external(a_open);
	}
}

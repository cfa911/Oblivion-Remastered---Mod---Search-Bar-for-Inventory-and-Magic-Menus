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
		};
		Api g_api;

		bool Resolve()
		{
			static ULONGLONG s_next = 0;
			if (g_api.enabled) return true;
			const ULONGLONG now = GetTickCount64();
			if (now < s_next) return false;
			s_next = now + 2000;   // looked for again every two seconds until it is there
			HMODULE m = GetModuleHandleW(L"OblivionMenuSearch.dll");
			if (!m) return false;
			Api a;
			a.version = reinterpret_cast<int (*)()>(GetProcAddress(m, "OblivionMenuSearch_ApiVersion"));
			a.enabled = reinterpret_cast<bool (*)(int)>(GetProcAddress(m, "OblivionMenuSearch_Enabled"));
			a.getText = reinterpret_cast<int (*)(int, char*, int)>(GetProcAddress(m, "OblivionMenuSearch_GetText"));
			a.setText = reinterpret_cast<void (*)(int, const char*)>(GetProcAddress(m, "OblivionMenuSearch_SetText"));
			a.external = reinterpret_cast<void (*)(bool)>(GetProcAddress(m, "OblivionMenuSearch_SetExternalInput"));
			if (!a.version || !a.enabled || !a.getText || !a.setText || !a.external || a.version() != 1) {
				static bool s_said = false;
				if (!s_said) {
					s_said = true;
					logger::warn("search: OblivionMenuSearch.dll is loaded but its interface is not the one this keyboard knows (version 1) - the keyboard stays off");
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

	void SetExternalInput(bool a_open)
	{
		if (Resolve()) g_api.external(a_open);
	}
}

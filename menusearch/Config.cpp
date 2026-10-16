#include "Config.h"

namespace cfg
{
	namespace
	{
		Values g_values;

		std::filesystem::path ThisModule()
		{
			HMODULE self = nullptr;
			wchar_t buf[MAX_PATH]{};
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&ThisModule), &self) &&
				GetModuleFileNameW(self, buf, MAX_PATH)) {
				return std::filesystem::path(buf);
			}
			return {};
		}

		double ReadDouble(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, double a_default, double a_min, double a_max)
		{
			wchar_t buf[64]{};
			GetPrivateProfileStringW(a_section, a_key, L"", buf, 64, a_ini.c_str());
			if (!buf[0]) return a_default;
			wchar_t*     end = nullptr;
			const double v = std::wcstod(buf, &end);
			return end == buf ? a_default : std::clamp(v, a_min, a_max);
		}

		int ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max)
		{
			const int v = static_cast<int>(GetPrivateProfileIntW(a_section, a_key, a_default, a_ini.c_str()));
			return std::clamp(v, a_min, a_max);
		}
	}

	void Load()
	{
		const auto ini = ThisModule().parent_path() / L"OblivionMenuSearch.ini";
		Values     v;
		if (std::filesystem::exists(ini)) {
			v.logLevel = ReadInt(ini, L"Log", L"uLogLevel", v.logLevel, 0, 4);
			v.inventory = ReadInt(ini, L"Search", L"bInventory", v.inventory ? 1 : 0, 0, 1) != 0;
			v.magic = ReadInt(ini, L"Search", L"bMagic", v.magic ? 1 : 0, 0, 1) != 0;
			v.barFontSize = ReadInt(ini, L"Bar", L"uFontSize", v.barFontSize, 8, 60);
			v.barX = ReadDouble(ini, L"Bar", L"uX", v.barX, 0, 3000);
			v.barY = ReadDouble(ini, L"Bar", L"uY", v.barY, 0, 3000);
			v.barYInventory = ReadDouble(ini, L"Bar", L"uYInventory", v.barYInventory, -200, 200);
			v.barYMagic = ReadDouble(ini, L"Bar", L"uYMagic", v.barYMagic, -200, 200);
			v.barW = ReadDouble(ini, L"Bar", L"uWidth", v.barW, 50, 3000);
			v.barH = ReadDouble(ini, L"Bar", L"uHeight", v.barH, 10, 400);
			v.ownFilter = ReadInt(ini, L"Filter", L"bEnabled", v.ownFilter ? 1 : 0, 0, 1) != 0;
			logger::info("config: {} read", ini.string());
		} else {
			logger::warn("config: {} not found - compiled defaults in use", ini.string());
		}
		g_values = v;
		logger::info("config: search in the Inventory {}, in the Magic menu {}, filter {}", v.inventory ? "on" : "off", v.magic ? "on" : "off", v.ownFilter ? "on" : "off");
	}

	std::filesystem::path PluginFolder()
	{
		return ThisModule().parent_path();
	}

	const Values& Get()
	{
		return g_values;
	}
}

#include "Probe.h"

#include "Config.h"
#include "Menus.h"
#include "PEHook.h"
#include "Reflect.h"
#include "Ui.h"

#include <map>
#include <set>

namespace probe
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		std::string              g_last;
		std::set<std::string>    g_dumped;

		std::string Lower(std::string a_s)
		{
			for (auto& c : a_s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return a_s;
		}

		bool Has(const std::string& a_lower, std::initializer_list<const char*> a_words)
		{
			for (const char* w : a_words) {
				if (a_lower.find(w) != std::string::npos) return true;
			}
			return false;
		}

		struct Seen
		{
			int          total = 0;
			int          shown = 0;
			UE::UObject* first = nullptr;
			UE::UObject* firstShown = nullptr;
		};
	}

	void Tick()
	{
		if (!cfg::Get().probe || !reflect::Ok()) return;
		static auto next = Clock::now();
		const auto  now = Clock::now();
		if (now < next) return;
		next = now + 1000ms;
		if (!menus::AnyOpen() || menus::Active() != menus::Menu::kNone) {
			g_last.clear();   // back in gameplay (or in the menus we do cover): the next menu is logged afresh
			return;
		}

		std::vector<std::pair<std::string, UE::UObject*>> found;
		if (auto* arr = UE::FUObjectArray::GetSingleton()) {
			arr->LockInternalArray();
			const std::int32_t n = arr->GetObjectArrayNum();
			for (std::int32_t i = 0; i < n; ++i) {
				auto* item = arr->IndexToObject(i);
				auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
				if (!o || (static_cast<std::int32_t>(o->objectFlags) & 0x30) != 0) continue;
				auto* cls = o->GetClass();
				if (!cls) continue;
				const auto name = pe::Utf8(cls->GetFName().ToString());
				const auto low = Lower(name);
				if (low.rfind("default__", 0) == 0) continue;
				if (Has(low, { "container", "barter", "trade", "shop", "merchant", "loot", "steal", "pickpocket", "haggle", "listview", "tileview" })) {
					found.emplace_back(name, o);
				}
			}
			arr->UnlockInternalArray();
		}

		std::map<std::string, Seen> seen;
		for (const auto& [name, o] : found) {
			auto& s = seen[name];
			++s.total;
			if (!s.first) s.first = o;
			if (!s.firstShown && ui::ShownOnScreen(o)) {
				s.firstShown = o;
			}
			if (s.firstShown == o) ++s.shown;
		}
		std::string signature;
		for (const auto& [name, s] : seen) {
			signature += std::format("{} x{} (shown {}), ", name, s.total, s.firstShown ? 1 : 0);
		}
		if (signature == g_last) return;
		g_last = signature;
		logger::info("probe: menu mode, no Inventory / Magic menu. Widgets of interest alive: {}", signature.empty() ? "none" : signature);

		for (const auto& [name, s] : seen) {
			const auto low = Lower(name);
			if (!s.firstShown || !Has(low, { "container", "barter", "trade", "shop", "merchant", "loot", "steal", "pickpocket", "haggle" })) continue;
			if (!g_dumped.insert(name).second) continue;
			logger::info("probe: {} is on screen: {}", name, pe::Utf8(s.firstShown->GetClass()->GetFullName()));
			ui::DumpTree(s.firstShown, 600);
		}
	}
}

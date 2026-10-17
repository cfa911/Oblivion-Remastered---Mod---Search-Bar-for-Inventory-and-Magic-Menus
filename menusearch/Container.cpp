#include "Container.h"

#include "PEHook.h"
#include "Reflect.h"
#include "Ui.h"

namespace container
{
	namespace
	{
		constexpr const wchar_t* kPath = L"/Game/UI/Modern/GameMenuLayer/Container_Inventory/WBP_ModernMenu_Container.WBP_ModernMenu_Container_C";

		std::atomic<UE::UClass*>    g_class{ nullptr };
		std::atomic<bool>           g_active{ false };
		reflect::Handle             g_widget;   // the widget last activated (game thread)
		std::atomic<UE::UFunction*> g_fnActivated{ nullptr };
		std::atomic<UE::UFunction*> g_fnDeactivated{ nullptr };

		void OnEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			if (!a_obj || !a_fn) return;
			bool activated = a_fn == g_fnActivated.load();
			bool deactivated = a_fn == g_fnDeactivated.load();
			if (!activated && !deactivated) {
				const std::string n = pe::FunctionName(a_fn);
				if (n == "BP_OnActivated") {
					g_fnActivated.store(a_fn);
					activated = true;
				} else if (n == "BP_OnDeactivated") {
					g_fnDeactivated.store(a_fn);
					deactivated = true;
				} else {
					return;
				}
			}
			g_active.store(activated);
			if (activated) g_widget = reflect::Hold(a_obj);
			logger::debug("container menu: {}", activated ? "activated" : "deactivated");
		}
	}

	void Tick()
	{
		if (g_class.load() || !reflect::Ok()) return;
		auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kPath);
		if (!cls || !pe::Watch(cls, &OnEvent)) return;
		g_class.store(cls);
		// the class may have been loaded long before the menu opens: it is open now only if an instance is on screen
		for (auto* o : reflect::Instances(cls)) {
			if (ui::ShownOnScreen(o)) {
				g_widget = reflect::Hold(o);
				g_active.store(true);
				break;
			}
		}
		logger::debug("container menu: watching its activation ({})", g_active.load() ? "open now" : "closed");
	}

	bool Open()
	{
		if (!menus::AnyOpen()) {
			g_active.store(false);   // back in gameplay: a missed deactivation is forgotten
			return false;
		}
		return g_active.load();
	}

	UE::UClass* Class() { return g_class.load(); }

	UE::UObject* Widget()
	{
		if (auto* w = reflect::Get(g_widget)) return w;
		if (auto* cls = g_class.load()) {
			for (auto* o : reflect::Instances(cls)) {
				g_widget = reflect::Hold(o);
				return o;
			}
		}
		return nullptr;
	}

	UE::UObject* SideContent(menus::Menu a_side)
	{
		auto* w = Widget();
		if (!w || (a_side != menus::Menu::kContainerYours && a_side != menus::Menu::kContainerTheirs)) return nullptr;
		return ui::FindInTree(w, a_side == menus::Menu::kContainerYours ? "cont_give_maincontent" : "cont_take_maincontent");
	}
}

#include "MenuSearch.h"

#include "Config.h"
#include "Container.h"
#include "PEHook.h"
#include "Quests.h"
#include "Reflect.h"
#include "Ui.h"
#include "UiText.h"

#include <cmath>
#include <cstring>
#include <unordered_set>

namespace msearch
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr std::size_t kMaxText = 40;

		// the text of the search bar (one per menu, ASCII), and the flags the window procedure shares with the game thread
		bool                g_conflict = false;   // another search mod is installed: everything here stands down
		std::mutex           g_qMutex;             // the window procedure may run on another thread than the game's
		std::string          g_queries[6];
				bool                 g_selAll[6] = {};   // Ctrl+A: the whole text is selected (the next key replaces it, Backspace / Delete clears it) - under g_qMutex   // by menu: kInventory, kMagic, kContainerYours, kContainerTheirs
		std::atomic<bool>    g_typing{ false };
		std::atomic<int>     g_typingMenu{ 0 };
		std::atomic<bool>    g_sideShown[5];   // the container / barter menu's sides that are on the screen now (a plain container shows only its own)
					double               g_canvasW = 1920;   // the container menu's root canvas, menu units (for which half of the screen a list is on)
					std::atomic<bool>    g_external{ false };   // the controller keyboard (another plugin) has the text: typed keys are not taken
		DWORD                g_gameThread = 0;

		bool IsSide(menus::Menu a_menu) { return a_menu == menus::Menu::kContainerYours || a_menu == menus::Menu::kContainerTheirs; }
		bool MenuEnabled(menus::Menu a_menu)
		{
			if (g_conflict) return false;
			return (a_menu == menus::Menu::kInventory && cfg::Get().inventory) || (a_menu == menus::Menu::kMagic && cfg::Get().magic)|| (IsSide(a_menu) && cfg::Get().container && g_sideShown[static_cast<int>(a_menu)].load()) || (a_menu == menus::Menu::kQuests && cfg::Get().quests);
		}
		bool OwnBarOn() { return !g_conflict && (cfg::Get().inventory || cfg::Get().magic || cfg::Get().container || cfg::Get().quests); }
		// a menu is present when it is the open one; in the container / barter menu both sides are
		bool Present(menus::Menu a_menu, menus::Menu a_active)
		{
			return a_menu == a_active || (IsSide(a_menu) && a_active == menus::Menu::kNone && container::Open()) || (a_menu == menus::Menu::kQuests && a_active == menus::Menu::kNone && quests::Open());
		}
		UE::UObject* MenuWidget(menus::Menu a_menu) { return IsSide(a_menu) ? container::Widget() : a_menu == menus::Menu::kQuests ? quests::Page() : menus::Widget(a_menu); }
		std::string QueryOf(menus::Menu a_menu)
		{
			std::scoped_lock l(g_qMutex);
			return g_queries[static_cast<int>(a_menu) % 6];
		}
		bool SelAllOf(menus::Menu a_menu)
				{
					std::scoped_lock l(g_qMutex);
					return g_selAll[static_cast<int>(a_menu) % 6];
				}
				void SetQuery(menus::Menu a_menu, const std::string& a_text)
{
			std::scoped_lock l(g_qMutex);
			g_queries[static_cast<int>(a_menu) % 6] = a_text;
		}

		// the user widget (the menu) a widget sits in
		UE::UObject* OwnerWidget(UE::UObject* a_o)
		{
			static auto* userWidget = ui::Class(L"/Script/UMG.UserWidget");
			for (auto* p = a_o ? a_o->GetOuter() : nullptr; p && userWidget; p = p->GetOuter()) {
				if (p->GetClass() && p->GetClass()->IsChildOf(userWidget)) return p;
			}
			return nullptr;
		}


		int ItemCount(UE::UObject* a_list)
		{
			if (!a_list) return -1;
			ui::Call n(a_list, L"GetNumItems");
			if (n && n.Run()) return n.Get<std::int32_t>("ReturnValue");
			ui::Call g(a_list, L"GetListItems");
			void*    ret = g ? g.At("ReturnValue") : nullptr;
			if (!ret || !g.Run()) return -1;
			const int num = *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(ret) + 8);   // TArray: data pointer, then the count
			if (void* data = *reinterpret_cast<void**>(ret)) UE::FMemory::Free(data);   // the engine allocated the copy
			return num;
		}

		UE::UObject* ListViewOf(UE::UObject* a_widget)
		{
			if (ItemCount(a_widget) >= 0) return a_widget;
			for (const wchar_t* fn : { L"GetListView", L"GetItemListView" }) {
				ui::Call g(a_widget, fn);
				if (g && g.Run()) {
					auto* inner = g.Get<UE::UObject*>("ReturnValue");
					if (inner && ItemCount(inner) >= 0) return inner;
				}
			}
			return nullptr;
		}

		// ---- reading the lists ----
		// The items of a list view (GetListItems copies the array: the copy's memory is ours to free)
		std::vector<UE::UObject*> ListItemsOf(UE::UObject* a_list)
		{
			std::vector<UE::UObject*> out;
			ui::Call                 g(a_list, L"GetListItems");
			void*                    ret = g ? g.At("ReturnValue") : nullptr;
			if (!ret || !g.Run()) return out;
			auto*      data = *reinterpret_cast<UE::UObject***>(ret);
			const auto num = *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(ret) + 8);
			for (std::int32_t i = 0; data && i < num && i < 4000; ++i) {
				if (data[i]) out.push_back(data[i]);
			}
			if (data) UE::FMemory::Free(data);
			return out;
		}

		// a list item's name: an FText-returning accessor if it has one, else the Properties struct's Name (as the row widgets carry it)
		// a name for a row of a kind we do not know the properties of (the quests tab): a name accessor if it has one, else the item's own text properties
		// (the ones called ...Name... or ...Title... first); the class and its text properties are logged once
		std::string GenericItemName(UE::UObject* a_item, const char** a_route)
		{
			static bool s_logged = false;
			std::vector<std::pair<std::string, std::string>> texts;   // property name, its text
			{   // the row's GetProperties() struct (LegacyMapMenuQuestProperties): its text properties
				static auto* props = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyMapMenuQuestProperties"));
				static bool  s_said = false;
				ui::Call     g(a_item, L"GetProperties");
				void*        ret = g && props ? g.At("ReturnValue") : nullptr;
				if (!s_said) {
					s_said = true;
					logger::debug("own filter (quests): GetProperties {}, the struct {}, its size {} / the call's {}", g ? "exists" : "is missing", props ? "is there" : "is missing", props ? props->propertiesSize : -1, g && props ? g.Size("ReturnValue") : -1);
					if (props) {
						for (const auto& [name, off] : reflect::Fields(props)) logger::debug("own filter (quests):   {}.{} @0x{:X} size {}", "LegacyMapMenuQuestProperties", name, off, ui::SizeOf(props, name));
					}
				}
				if (ret && g.Size("ReturnValue") == props->propertiesSize && g.Run()) {
					for (const auto& [name, off] : reflect::Fields(props)) {
						const auto size = ui::SizeOf(props, name);
						if (off < 0 || size != static_cast<std::int32_t>(sizeof(UE::FText)) || off + size > props->propertiesSize) continue;
						auto* t = reinterpret_cast<UE::FText*>(static_cast<std::uint8_t*>(ret) + off);
						if (reflect::TextSet(*t)) texts.emplace_back(name, reflect::Text(*t));
						t->~FText();
					}
				}
			}
for (UE::UStruct* c = a_item->GetClass(); c; c = c->GetSuperStruct()) {
				if (pe::Utf8(c->GetFName().ToString()) == "Object") break;
				for (const auto& [name, off] : reflect::Fields(c)) {
					const auto size = ui::SizeOf(c, name);
					if (off < 0 || size != static_cast<std::int32_t>(sizeof(UE::FText)) || off + size > c->propertiesSize) continue;
					const auto& t = *reinterpret_cast<const UE::FText*>(reinterpret_cast<const std::uint8_t*>(a_item) + off);
					if (!reflect::TextSet(t)) continue;
					texts.emplace_back(name, reflect::Text(t));
				}
			}
			if (!s_logged) {
				s_logged = true;
				logger::debug("own filter (quests): a list item is {}; its text properties: {}", pe::Utf8(a_item->GetClass()->GetFName().ToString()), texts.empty() ? std::string("none") : std::string());
				for (const auto& [n, t] : texts) logger::debug("own filter (quests):   {} = {}", n, t.substr(0, 60));
			}
			for (const char* want : { "name", "title" }) {
				for (const auto& [n, t] : texts) {
					std::string low = n;
					for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					if (!t.empty() && low.find(want) != std::string::npos) {
						if (a_route) *a_route = "text property";
						return t;
					}
				}
			}
			std::string all;
			for (const auto& [n, t] : texts) all += t + " ";
			if (!all.empty() && a_route) *a_route = "text properties";
			return all;
		}

		std::string ItemName(UE::UObject* a_item, menus::Menu a_menu, const char** a_route = nullptr)
{
			static auto* magicProps = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyMagicMenuItemProperties"));
			static auto* invProps = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.OriginalInventoryMenuItemProperties"));
			if (!a_item || !a_item->GetClass() || !reflect::Ok() || !reflect::IsLive(a_item)) return {};
			for (const wchar_t* fn : { L"GetNameText", L"GetDisplayNameText" }) {
				ui::Call c(a_item, fn);
				void*    ret = c ? c.At("ReturnValue") : nullptr;
				if (!ret || c.Size("ReturnValue") != static_cast<std::int32_t>(sizeof(UE::FText)) || !c.Run()) continue;
				auto*       t = static_cast<UE::FText*>(ret);
				std::string s = reflect::Text(*t);
				t->~FText();
				if (!s.empty()) {
					if (a_route) *a_route = fn[3] == L'N' ? "GetNameText" : "GetDisplayNameText";
					return s;
				}
			}
			if (a_menu == menus::Menu::kQuests) return GenericItemName(a_item, a_route);
						auto* props = a_menu == menus::Menu::kMagic ? magicProps : invProps;
			ui::Call g(a_item, L"GetProperties");
			void*    ret = g && props ? g.At("ReturnValue") : nullptr;
			const auto nameOff = props ? reflect::Offset(props, "Name") : -1;
			if (!ret || nameOff < 0 || g.Size("ReturnValue") != props->propertiesSize || !g.Run()) return {};
			auto*       t = reinterpret_cast<UE::FText*>(static_cast<std::uint8_t*>(ret) + nameOff);
			std::string s = reflect::Text(*t);
			t->~FText();
			if (a_route && !s.empty()) *a_route = "GetProperties";
			return s;
		}

		std::string Lower(std::string a_s)
		{
			for (auto& c : a_s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return a_s;
		}

		struct FilterState
	{
		reflect::Handle              list;
		Clock::time_point             nextLook{};
		std::vector<reflect::Handle>  source;    // the full list, as the game last built it
		std::vector<UE::UObject*>     applied;   // what the list holds after our last rewrite (raw pointers: only compared, never used)
		std::string                   appliedText;
		Clock::time_point             nextSnapshot{};   // with no search: the next time the whole list is copied again
		std::vector<UE::UObject*>     evaluated;   // the list as last looked at, and the text it was looked at with
		std::string                   evaluatedText;
		bool                          active = false, watching = false;
		void Reset()
		{
			list = {};
			source.clear();
			applied.clear();
			appliedText.clear();
			evaluated.clear();
			evaluatedText.clear();
			active = false;
			watching = false;
		}
	};

		FilterState g_filter[6];   // by menu

		// ---- our own search bar (Inventory and Magic) ----
		// A bar in the menu's own root canvas, where the search mods put theirs, and the typing for it: while it is being typed into, the
		// game window's keys are taken here (the menu's hotkeys never see them), Ctrl+F / Ctrl+K or a click on the bar starts typing, Enter or
		// Esc ends it. The text lives in g_queries; the filter and the controller's keyboard read and write the same text.
		constexpr float kBarFill[4] = { 0.16f, 0.09f, 0.04f, 1.0f }, kBarEdge[4] = { 0.06f, 0.035f, 0.018f, 1.0f }, kBarFocus[4] = { 0.78f, 0.62f, 0.28f, 1.0f };
		constexpr float kBarInk[4] = { 0.93f, 0.90f, 0.82f, 1.0f }, kBarHint[4] = { 0.58f, 0.53f, 0.45f, 1.0f };

		struct BarWidgets
		{
			reflect::Handle   canvas, frame, edge, label, frameSlot, labelSlot;
			bool              textured = false;
			std::string       shown;
			std::string       hint = "Search";   // what the empty bar says (the container / barter menu's say whose list it is)
			bool              typing = false;
			bool              visible = true;
			double            x = 0, y = 0, w = 0, h = 0;   // where it sits in its canvas (menu units)
			Clock::time_point nextLook{}, nextMeasure{};
		};
		BarWidgets g_bar[6];
		struct PxRect { double x = 0, y = 0, w = 0, h = 0; bool valid = false; };
		PxRect  g_barPx[6];   // the bar's place in window pixels (g_qMutex)
		HWND    g_hwnd = nullptr;
		WNDPROC  g_origProc = nullptr;

		float ViewportScale()
		{
			static auto* lib = ui::Class(L"/Script/UMG.WidgetLayoutLibrary");
			auto*        cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			auto*        pc = ui::PlayerController();
			if (!cdo || !pc) return 1.0f;
			ui::Call c(cdo, L"GetViewportScale");
			if (!c) return 1.0f;
			c.Set("WorldContextObject", pc);
			return c.RunGuarded() ? c.Get<float>("ReturnValue") : 1.0f;
		}

		// the root canvas the bar goes into: that of the user widget that holds the list (the Inventory's search bar is in a widget inside the
		// menu), else the menu's own
		UE::UObject* BarCanvasFor(menus::Menu a_menu, bool a_log = true)
		{
			static auto* canvasClass = ui::Class(L"/Script/UMG.CanvasPanel");
			static auto* treeClass = ui::Class(L"/Script/UMG.WidgetTree");
			auto*        menuW = MenuWidget(a_menu);
							if (!menuW || !canvasClass || !treeClass || !reflect::Ok()) return nullptr;
			auto* wrapper = IsSide(a_menu) || a_menu == menus::Menu::kQuests ? nullptr : ui::FindInTree(menuW, a_menu == menus::Menu::kMagic ? "Magic_Spell_List" : "inv_cont_listview");
			auto* owner = wrapper ? OwnerWidget(wrapper) : nullptr;
			std::vector<UE::UObject*> cands{ menuW, owner };
						if (a_menu == menus::Menu::kQuests) {
							for (auto* p = OwnerWidget(menuW); p; p = OwnerWidget(p)) cands.push_back(p);
						}
						for (auto* w : cands) {
				if (!w || !w->GetClass()) continue;
				const auto wsOff = reflect::Offset(w->GetClass(), "WidgetTree");
				auto**      tree = wsOff >= 0 ? reflect::At<UE::UObject*>(w, wsOff) : nullptr;
				if (!tree || !*tree) continue;
				const auto rootOff = reflect::Offset(treeClass, "RootWidget");
				auto**      root = rootOff >= 0 ? reflect::At<UE::UObject*>(*tree, rootOff) : nullptr;
				if (root && *root && (*root)->GetClass() && (*root)->GetClass()->IsChildOf(canvasClass)) {
					if (a_log) logger::debug("own bar: the {}'s bar goes into the root canvas of {}", menus::Name(a_menu), pe::Utf8(w->GetClass()->GetFName().ToString()));
					return *root;
				}
			}
			return nullptr;
		}

		// the bar's panel: a PNG from the plugin's folder (black for the Inventory and the Magic menu, green for your side of the container / barter menu, red
		// for the other side), else the black one, else a plain rounded box
		UE::UObject* BarTexture(menus::Menu a_menu)
		{
			const int slot = a_menu == menus::Menu::kContainerYours ? 1 : a_menu == menus::Menu::kContainerTheirs ? 2 : 0;
			static reflect::Handle s_tex[3];
			static bool            s_failed[3] = { false, false, false };
			if (auto* t = reflect::Get(s_tex[slot])) return t;
			if (s_failed[slot]) return slot ? BarTexture(menus::Menu::kInventory) : nullptr;
			const wchar_t* file = slot == 1 ? L"SearchBarYours.png" : slot == 2 ? L"SearchBarTheirs.png" : L"SearchBar.png";
			const auto     path = cfg::PluginFolder() / L"MenuSearch" / file;
			static auto*   lib = ui::Class(L"/Script/Engine.KismetRenderingLibrary");
			ui::Call       c(lib ? lib->GetDefaultObject(false) : nullptr, L"ImportFileAsTexture2D");
			void*          fname = c.At("Filename");
			UE::UObject*   tex = nullptr;
			if (c && fname && std::filesystem::exists(path)) {
				c.Set("WorldContextObject", ui::PlayerController());
				auto* str = new (fname) UE::FString(path.wstring().c_str());
				tex = c.RunGuarded() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
				str->~FString();
			}
			if (!tex) {
				s_failed[slot] = true;
				logger::warn("own bar: the bar image could not be loaded ({}{}) - {}", path.string(), std::filesystem::exists(path) ? "" : ", missing", slot ? "the black one is used" : "a plain box is used");
				return slot ? BarTexture(menus::Menu::kInventory) : nullptr;
			}
			s_tex[slot] = reflect::Hold(tex);
			logger::debug("own bar: the bar image is loaded from {}", path.filename().string());
			return tex;
		}

		// a side of the container / barter menu is on the screen (a plain container shows only its own list)
		bool SideShownNow(menus::Menu a_side)
		{
			auto* content = container::SideContent(a_side);
			return content && ui::ShownOnScreen(content);
		}

		// the container / barter menu's bar: above the side's header (menu units, local to the menu's root canvas); the container's list alone on the
		// screen (the player's side not shown) has a place of its own
		bool ContainerBarRect(menus::Menu a_side, UE::UObject* a_canvas, double& a_x, double& a_y, double& a_w, double& a_h, bool a_log)
		{
			auto* content = container::SideContent(a_side);
			auto* header = content ? ui::FindInTree(content, "inv_cont_header") : nullptr;
			double hx = 0, hy = 0, hw = 0, hh = 0, cx = 0, cy = 0, cw = 0, ch = 0;
			if (!header || !ui::Measure(header, hx, hy, hw, hh) || !ui::Measure(a_canvas, cx, cy, cw, ch) || hw < 10 || hh < 1) return false;
			const auto& c = cfg::Get();
			// the container's list without the tab row above it (a plain container, nothing to give) has a place of its own; with the tabs (beside yours in a
			// transfer or a barter, or beside an item's details) it sits like the barter's
			auto*       menuW = container::Widget();
			auto*       tabs = menuW ? ui::FindInTree(menuW, a_side == menus::Menu::kContainerYours ? "cont_give_tabs" : "cont_take_tabs") : nullptr;
			const bool  take = a_side == menus::Menu::kContainerTheirs && !(tabs && ui::ShownOnScreen(tabs));
			const auto& p = take ? c.takeBar : a_side == menus::Menu::kContainerYours ? c.yoursBar : c.theirsBar;
			a_h = p.h;
			a_w = p.w > 0 ? p.w : hw + 2 * std::abs(p.x);
			a_x = hx - cx + p.x;
			a_y = hy - cy - a_h - 6 + p.y;
			if (cw > 100) g_canvasW = cw;
			if (a_log) {
				logger::debug("own bar: {}{} - header at {:.0f},{:.0f} {:.0f}x{:.0f}, canvas at {:.0f},{:.0f} {:.0f}x{:.0f} -> bar at {:.0f},{:.0f} {:.0f}x{:.0f}", menus::Name(a_side), take ? " (its list alone)" : "", hx, hy, hw, hh, cx, cy, cw, ch, a_x, a_y, a_w, a_h);
				for (const char* name : { "inv_cont_title", "inv_cont_filter", "inv_cont_listview_overlay" }) {
					double x = 0, y = 0, w = 0, h = 0;
					if (auto* o = ui::FindInTree(content, name); o && ui::Measure(o, x, y, w, h)) logger::debug("own bar:   {} at {:.0f},{:.0f} {:.0f}x{:.0f}", name, x, y, w, h);
				}
			}
			return true;
		}

		// the quests tab's bar sits where the Magic menu's does; what the canvas it went into and the quest panels measure is logged, to check that
		void LogQuestLayout(UE::UObject* a_canvas)
		{
			double x = 0, y = 0, w = 0, h = 0;
			if (ui::Measure(a_canvas, x, y, w, h)) logger::debug("own bar: the quests tab - the canvas the bar goes into is at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* page = quests::Page(); page && ui::Measure(page, x, y, w, h)) logger::debug("own bar:   quests page at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* panel = quests::ListPanel(); panel && ui::Measure(panel, x, y, w, h)) logger::debug("own bar:   quest list panel at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* list = quests::ListWrapper(); list && ui::Measure(list, x, y, w, h)) logger::debug("own bar:   quest list at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
		}

bool BuildBar(menus::Menu a_menu, UE::UObject* a_canvas)
{
			static int         s_n = 0;
			const std::wstring n = std::to_wstring(++s_n);
			const auto&        c = cfg::Get();
			const auto&        place = a_menu == menus::Menu::kMagic ? c.magicBar : a_menu == menus::Menu::kQuests ? c.questBar : c.inventoryBar;
			double             barX = place.x, barW = place.w, barH = place.h;
			double             barY = place.y;
			if (IsSide(a_menu) && !ContainerBarRect(a_menu, a_canvas, barX, barY, barW, barH, true)) return false;
				if (a_menu == menus::Menu::kQuests) LogQuestLayout(a_canvas);
			UE::UObject* slot = nullptr;
			auto*        frame = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", (L"IwmBarFrame" + n).c_str(), &slot);
			if (!frame || !slot) return false;
			ui::Vec2(slot, L"SetPosition", barX, barY);
			ui::Vec2(slot, L"SetSize", barW, barH);
			const std::int32_t z = 1000;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			bool textured = false;
			if (auto* tex = BarTexture(a_menu)) {
				ui::Call b(frame, L"SetBrushFromTexture");
				b.Set("Texture", tex);
				b.Set("bMatchSize", false);
				textured = b.Run();
			}
			if (!textured) ui::RoundedBox(frame, false, kBarFill, kBarEdge, 2.0f);
			ui::Visible(frame, true);
			UE::UObject* lslot = nullptr;
			auto*        label = ui::AddToCanvas(a_canvas, L"/Script/UMG.TextBlock", (L"IwmBarText" + n).c_str(), &lslot);
			if (!label || !lslot) return false;
			ui::Vec2(lslot, L"SetAlignment", 0.5, 0.5);
			const bool yes = true;
			ui::CallFirst(lslot, L"SetAutoSize", &yes, sizeof(yes));
			ui::Vec2(lslot, L"SetPosition", barX + barW * 0.5, barY + barH * 0.5);
			const std::int32_t z2 = 1001;
			ui::CallFirst(lslot, L"SetZOrder", &z2, sizeof(z2));
			ui::Visible(label, true);
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			B.canvas = reflect::Hold(a_canvas);
			B.frame = reflect::Hold(frame);
			B.frameSlot = reflect::Hold(slot);
			B.textured = textured;
			B.label = reflect::Hold(label);
			B.labelSlot = reflect::Hold(lslot);
			B.x = barX;
			B.y = barY;
			B.w = barW;
			B.h = barH;
			B.visible = true;
			B.shown = "\x01";   // matches nothing real: the first update draws it
			B.typing = false;
			logger::debug("own bar: built in the {}", menus::Name(a_menu));
			return true;
		}

		// a side that is not on the screen has no bar
		void HideBar(menus::Menu a_menu)
		{
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			if (!B.visible) return;
			if (auto* f = reflect::Get(B.frame)) ui::Visible(f, false);
			if (auto* l = reflect::Get(B.label)) ui::Visible(l, false);
			B.visible = false;
			g_barPx[static_cast<int>(a_menu)].valid = false;
		}

		void UpdateBar(menus::Menu a_menu, Clock::time_point a_now)
		{
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			auto*       label = reflect::Get(B.label);
			auto*       frame = reflect::Get(B.frame);
			if (!label || !frame) return;
			if (!B.visible) {
				ui::Visible(frame, true);
				ui::Visible(label, true);
				B.visible = true;
			}
			const bool        typing = g_typing.load() && g_typingMenu.load() == static_cast<int>(a_menu);
				if (!typing) {
					std::scoped_lock l(g_qMutex);
					g_selAll[static_cast<int>(a_menu) % 6] = false;
				}
				const std::string q = QueryOf(a_menu);
			const bool        caret = typing && (GetTickCount64() / 500) % 2 == 0;
			const bool        hint = q.empty() && !typing;
			const std::string show = hint ? B.hint : q + (typing ? (caret ? "|" : " ") : "");
			const bool        sel = typing && !q.empty() && SelAllOf(a_menu);   // selected: drawn in the bar's gold
				const std::string key = std::string(hint ? "#" : sel ? "*" : "=") + show;
			if (key != B.shown) {
				uix::SetText(label, show);
				uix::Ink(label, hint ? kBarHint : sel ? kBarFocus : kBarInk);
				uix::ApplyFont(label, cfg::Get().barFontSize);
				B.shown = key;
			}
			if (typing != B.typing) {   // the bronze edge while it is being typed into
				if (!B.textured) ui::RoundedBox(frame, false, kBarFill, typing ? kBarFocus : kBarEdge, 2.0f);
				B.typing = typing;
			}
			if (a_now >= B.nextMeasure) {   // where it is in window pixels, for a click on it
				B.nextMeasure = a_now + 150ms;
				if (IsSide(a_menu)) {
					// yours says Player; the other side Seller in a barter (the merchant's gold row is on screen), else Container
					std::string want = "Player";
					if (a_menu == menus::Menu::kContainerTheirs) {
						auto* money = container::Widget() ? ui::FindInTree(container::Widget(), "cont_take_money_verticalbox") : nullptr;
						want = money && ui::ShownOnScreen(money) ? "Seller" : "Container";
					}
					if (want != B.hint) {
						B.hint = want;
						logger::debug("own bar: the {}'s empty bar says {}", menus::Name(a_menu), want);
					}
					// the layout can change (the container's list alone, or beside yours): the bar follows its header
					auto*  canvas = reflect::Get(B.canvas);
					auto*  slot = reflect::Get(B.frameSlot);
					auto*  lslot = reflect::Get(B.labelSlot);
					double x = 0, y = 0, w = 0, h = 0;
					if (canvas && slot && lslot && ContainerBarRect(a_menu, canvas, x, y, w, h, false) &&
						(std::abs(x - B.x) > 0.5 || std::abs(y - B.y) > 0.5 || std::abs(w - B.w) > 0.5 || std::abs(h - B.h) > 0.5)) {
						ui::Vec2(slot, L"SetPosition", x, y);
						ui::Vec2(slot, L"SetSize", w, h);
						ui::Vec2(lslot, L"SetPosition", x + w * 0.5, y + h * 0.5);
						B.x = x;
						B.y = y;
						B.w = w;
						B.h = h;
						logger::debug("own bar: the {}'s bar moved to {:.0f},{:.0f} {:.0f}x{:.0f}", menus::Name(a_menu), x, y, w, h);
					}
				}
				double x = 0, y = 0, w = 0, h = 0;
				if (ui::Measure(frame, x, y, w, h)) {
					const double s = ViewportScale();
					std::scoped_lock l(g_qMutex);
					g_barPx[static_cast<int>(a_menu)] = { x * s, y * s, w * s, h * s, true };
				}
			}
		}

		// ---- the typing: the game window's messages ----
		void PasteInto(std::string& a_q)
		{
			if (!OpenClipboard(nullptr)) return;
			if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
				if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
					for (; *w && a_q.size() < kMaxText; ++w) {
						if (*w >= 32 && *w < 127) a_q.push_back(static_cast<char>(*w));
					}
					GlobalUnlock(h);
				}
			}
			CloseClipboard();
		}

		void Typed(wchar_t a_c, menus::Menu a_menu)
		{
			std::scoped_lock l(g_qMutex);
			auto&            q = g_queries[static_cast<int>(a_menu) % 6];
			bool&            sel = g_selAll[static_cast<int>(a_menu) % 6];
			if (a_c == 8) {
				if (sel) q.clear();
				else if (!q.empty()) q.pop_back();
				sel = false;
			} else if (a_c == 0x16) {   // Ctrl+V
				if (sel) q.clear();
				sel = false;
				PasteInto(q);
			} else if (a_c >= 32 && a_c < 127) {
				if (sel) q.clear();   // a selected text is replaced by what is typed
				sel = false;
				if (q.size() < kMaxText) q.push_back(static_cast<char>(a_c));
			}
		}

		bool InsideBar(menus::Menu a_menu, LPARAM a_l)
		{
			const int x = static_cast<short>(LOWORD(a_l)), y = static_cast<short>(HIWORD(a_l));
			std::scoped_lock l(g_qMutex);
			const auto& r = g_barPx[static_cast<int>(a_menu) % 6];
			return r.valid && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
		}

		bool ControllerKeyboardUp() { return g_external.load(); }

		// the container / barter menu's side the mouse is over: yours is on the left (cont_give), the other on the right
		menus::Menu SideUnderCursor()
		{
			const bool yours = g_sideShown[static_cast<int>(menus::Menu::kContainerYours)].load(), theirs = g_sideShown[static_cast<int>(menus::Menu::kContainerTheirs)].load();
			if (yours && !theirs) return menus::Menu::kContainerYours;
			if (theirs && !yours) return menus::Menu::kContainerTheirs;
			POINT pt{};
			RECT  rc{};
			if (g_hwnd && GetCursorPos(&pt) && ScreenToClient(g_hwnd, &pt) && GetClientRect(g_hwnd, &rc) && pt.x > (rc.right - rc.left) / 2) return menus::Menu::kContainerTheirs;
			return menus::Menu::kContainerYours;
		}

		constexpr UINT_PTR kTimerId = 0x4D53;

		LRESULT CALLBACK BarProc(HWND a_h, UINT a_m, WPARAM a_w, LPARAM a_l)
		{
			if (a_m == WM_TIMER && a_w == kTimerId) {   // the timer set below: a tick for a player whose controller the game barely polls
				static bool s_in = false;
				if (GetCurrentThreadId() == g_gameThread && !s_in) {
					s_in = true;
					Tick(menus::Active());
					s_in = false;
				}
				return 0;
			}
			if (OwnBarOn()) {
				auto       menu = menus::Active();
				const bool cont = menu == menus::Menu::kNone && container::Open();
				if (cont) menu = g_typing.load() ? static_cast<menus::Menu>(g_typingMenu.load()) : SideUnderCursor();   // the container / barter menu: the side typed into, else the one the mouse is on
				if (!cont && menu == menus::Menu::kNone && quests::Open()) menu = menus::Menu::kQuests;
				if (MenuEnabled(menu)) {
					if (g_typing.load()) {
						switch (a_m) {
						case WM_KEYDOWN:   // a key typed into the bar: nothing else sees it (the menu's hotkeys stay quiet)
							if (a_w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {   // Ctrl+A: select all
															std::scoped_lock l(g_qMutex);
															g_selAll[static_cast<int>(menu) % 6] = !g_queries[static_cast<int>(menu) % 6].empty();
															return 0;
														}
														if (a_w == VK_DELETE) {   // Delete clears a selected text
															std::scoped_lock l(g_qMutex);
															if (g_selAll[static_cast<int>(menu) % 6]) g_queries[static_cast<int>(menu) % 6].clear();
															g_selAll[static_cast<int>(menu) % 6] = false;
															return 0;
														}
														if (a_w == VK_ESCAPE || a_w == VK_RETURN) g_typing = false;
							return 0;
						case WM_CHAR:
							Typed(static_cast<wchar_t>(a_w), menu);
							return 0;
						case WM_LBUTTONDOWN:
							if (cont && !InsideBar(menu, a_l)) {   // the other side's bar: typing moves there
								const auto other = menu == menus::Menu::kContainerYours ? menus::Menu::kContainerTheirs : menus::Menu::kContainerYours;
								if (MenuEnabled(other) && InsideBar(other, a_l)) {
									g_typingMenu = static_cast<int>(other);
									return 0;
								}
							}
							if (!InsideBar(menu, a_l)) g_typing = false;   // a click elsewhere ends it and goes on to the game
							break;
						default: break;
						}
					} else if (!ControllerKeyboardUp()) {
						const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
						if (a_m == WM_KEYDOWN && a_w == 'F' && ctrl && !alt) {
							g_typingMenu = static_cast<int>(menu);
							g_typing = true;
							return 0;
						}
						if (a_m == WM_LBUTTONDOWN) {
							const auto other = !cont ? menus::Menu::kNone : menu == menus::Menu::kContainerYours ? menus::Menu::kContainerTheirs : menus::Menu::kContainerYours;
							for (const auto m : { menu, other }) {
								if (m != menus::Menu::kNone && MenuEnabled(m) && InsideBar(m, a_l)) {
									g_typingMenu = static_cast<int>(m);
									g_typing = true;
									return 0;
								}
							}
						}
					}
				} else if (g_typing.load()) {
					g_typing = false;
				}
			}
			return CallWindowProcW(g_origProc, a_h, a_m, a_w, a_l);
		}

		BOOL CALLBACK FindGameWindow(HWND a_w, LPARAM a_p)
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(a_w, &pid);
			if (pid != GetCurrentProcessId() || !IsWindowVisible(a_w) || GetWindow(a_w, GW_OWNER)) return TRUE;
			wchar_t cls[64]{};
			GetClassNameW(a_w, cls, 64);
			if (std::wcscmp(cls, L"UnrealWindow") != 0) return TRUE;
			*reinterpret_cast<HWND*>(a_p) = a_w;
			return FALSE;
		}

		void InstallWndProc()
		{
			static ULONGLONG s_next = 0;
			const ULONGLONG   now = GetTickCount64();
			if (g_origProc || now < s_next) return;
			s_next = now + 1000;
			HWND found = nullptr;
			EnumWindows(&FindGameWindow, reinterpret_cast<LPARAM>(&found));
			if (!found) return;
			g_hwnd = found;
			g_origProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(found, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&BarProc)));
			logger::debug("own bar: the game window's keys and clicks are watched for typing ({})", g_origProc ? "ok" : "FAILED");
			if (g_origProc) {
				const DWORD windowThread = GetWindowThreadProcessId(found, nullptr);
				if (g_gameThread && windowThread == g_gameThread) {   // the window's messages are handled on the game's thread: a timer there is a safe extra tick
					SetTimer(found, kTimerId, 8, nullptr);
					logger::debug("own bar: the game window is on the game thread ({}) - a timer on it ticks the search too", windowThread);
				} else {
					logger::debug("own bar: the game window is on thread {}, the game thread is {} - the search is ticked by the controller read only", windowThread, g_gameThread);
				}
			}
		}
	}

	// ---- the filter ----
	// The search text is that of the search bar. A copy of the full list - the items as the game built them -
	// is kept while nothing is searched; with a search active the list is rewritten to the matching items (a case-insensitive part of the
	// item's name) by ClearListItems / AddItem. When the game rebuilds the whole list (after a press) it is filtered again on the next read
	// of the controller, a frame at most - the search mods take a worker-thread wake or two (up to ~130 ms) for the same.
	static void ApplyItems(UE::UObject* a_list, const std::vector<UE::UObject*>& a_items)
	{
		ui::Call clear(a_list, L"ClearListItems");
		clear.Run();
		for (auto* it : a_items) {
			ui::CallFirst(a_list, L"AddItem", &it, sizeof(it));
		}
		ui::Call regen(a_list, L"RegenerateAllEntries");
		regen.Run();
		ui::Call refresh(a_list, L"RequestRefresh");
		refresh.Run();
	}

	static void RegisterMenuHooks();

	void FilterTick(menus::Menu a_menu, Clock::time_point a_now)
	{
		RegisterMenuHooks();
		// a menu that is not the open one forgets everything it held (in the container / barter menu both sides are open)
		const auto context = menus::Active();
		for (int m = 1; m <= 5; ++m) {
			if (!Present(static_cast<menus::Menu>(m), context) && g_filter[m].watching) g_filter[m].Reset();
		}
		if (!cfg::Get().ownFilter || !reflect::Ok() || !MenuEnabled(a_menu)) return;
		FilterState& S = g_filter[static_cast<int>(a_menu)];
		const char* menuName = menus::Name(a_menu);
		if (!reflect::Get(S.list)) {   // the list, found a couple of times a second until it is
			if (a_now < S.nextLook) return;
			S.nextLook = a_now + 500ms;
			UE::UObject* list = nullptr;
			if (IsSide(a_menu)) {
				auto* content = container::SideContent(a_menu);
				list = content ? ui::FindInTree(content, "inv_cont_listview") : nullptr;
			} else if (a_menu == menus::Menu::kQuests) {
				list = quests::ListWrapper();
			} else {
				auto* menuW = menus::Widget(a_menu);
				list = menuW ? ui::FindInTree(menuW, a_menu == menus::Menu::kMagic ? "Magic_Spell_List" : "inv_cont_listview") : nullptr;
			}
			auto* view = list ? ListViewOf(list) : nullptr;
			if (!view) return;
			S.list = reflect::Hold(view);
			S.watching = true;
			logger::debug("own filter: watching the list of the {}", menuName);
			if (IsSide(a_menu) || a_menu == menus::Menu::kQuests) {   // what the rows are: their class and how their names read
				const auto  items = ListItemsOf(view);
				const char* route = "none";
				const auto  first = items.empty() ? std::string() : ItemName(items.front(), a_menu, &route);
				logger::debug("own filter ({}): the list holds {} items; the first is {} named \"{}\" (read by {})", menuName, items.size(),
					items.empty() ? std::string("n/a") : pe::Utf8(items.front()->GetClass()->GetFName().ToString()), first, route);
			}
		}
		auto* list = reflect::Get(S.list);
		if (!list) return;
		const auto text = QueryOf(a_menu);
		const auto query = Lower(text);
		if (query.empty() && !S.active && a_now < S.nextSnapshot && ItemCount(list) == static_cast<int>(S.source.size())) return;   // nothing searched, nothing changed: no copy of the list
		S.nextSnapshot = a_now + 250ms;
		const auto live = ListItemsOf(list);
		if (query.empty()) {
			// no search: the list is whole - keep a copy of it, and give it back whole if we had cut it down
			if (S.active) {
				std::vector<UE::UObject*> all;
				for (const auto& h : S.source) {
					if (auto* o = reflect::Get(h)) all.push_back(o);
				}
				if (all.size() == S.source.size() && !all.empty()) ApplyItems(list, all);
				S.active = false;
				S.applied.clear();
				S.appliedText.clear();
				logger::debug("own filter ({}): the search was cleared - the list given back whole ({} items)", menuName, all.size());
			} else if (live.size() != S.source.size() || (!live.empty() && reflect::Get(S.source.front()) != live.front())) {
				S.source.clear();
				for (auto* o : live) S.source.push_back(reflect::Hold(o));
			}
			return;
		}
		// a search is active
		if (S.active && query == S.appliedText && live == S.applied) return;   // as we left it
		if (S.evaluated == live && S.evaluatedText == query) return;           // looked at already (someone else's equal filter), nothing new
		S.evaluated = live;
		S.evaluatedText = query;
		// Is this the game's whole list? A filtered list holds only matches (both mods filter by the same rule) and only items we already
		// know from the whole list; a list with a non-matching item and an item never seen is the game's own - a rebuild after a press, a tab,
		// the menu opening with the search already typed.
		std::unordered_set<UE::UObject*> known;
		for (const auto& h : S.source) {
			if (auto* o = reflect::Get(h)) known.insert(o);
		}
		bool nonMatching = false, foreign = false;
		for (auto* o : live) {
			if (!known.count(o)) foreign = true;
			if (!nonMatching && Lower(ItemName(o, a_menu)).find(query) == std::string::npos) nonMatching = true;
		}
		const bool whole = nonMatching && (known.empty() || foreign);
		const bool rebuilt = whole || (nonMatching && S.active && query == S.appliedText);
		if (whole) {
			S.source.clear();
			for (auto* o : live) S.source.push_back(reflect::Hold(o));
		} else if (S.source.empty()) {
			return;   // the whole list is not known yet (the search was already there and the list is already cut): left to the other mod
		} else if (S.active && query == S.appliedText && !nonMatching) {
			return;   // all match and the text is the one applied: it is filtered, by us or by the other mod
		}
		if (S.source.empty()) return;   // the whole list is not known yet (the search was already there and the list is already cut): left to the other mod
		std::vector<UE::UObject*> source, keep;
		for (const auto& h : S.source) {
			auto* o = reflect::Get(h);
			if (!o) {   // the items it copied are gone: start over from what the game gives next
				S.source.clear();
				return;
			}
			source.push_back(o);
		}
		if (a_menu == menus::Menu::kQuests) {   // rows whose names cannot be read: the list is left whole (never emptied by a search that cannot match)
			bool any = false;
			for (auto* o : source) {
				if (!ItemName(o, a_menu).empty()) {
					any = true;
					break;
				}
			}
			if (!any) {
				static bool s_said = false;
				if (!s_said) {
					s_said = true;
					logger::warn("own filter (quests): no list item has a readable name - the list is left whole");
				}
				return;
			}
		}
		for (auto* o : source) {
					if (Lower(ItemName(o, a_menu)).find(query) != std::string::npos) keep.push_back(o);
		}
		ApplyItems(list, keep);
		S.applied = keep;
		S.appliedText = query;
		S.active = true;
		S.evaluated.clear();   // what the list holds now is ours: looked at fresh next time
		static int s_applies = 0;
		if (++s_applies <= 40 || s_applies % 50 == 0) {   // debug level: the hover rebuilds the list all the time
			logger::debug("own filter ({}): \"{}\" keeps {} of {} items ({}; {} so far)", menuName, text, keep.size(), source.size(), rebuilt ? "the game's whole list" : "the search text changed", s_applies);
		}
	}

	// Run after one of the open menu's own calls (a press handled, a tab changed): if the list no longer is what we left it, filter it now -
	// still in the frame the game rebuilt it in, before anything is drawn. One cheap count when nothing changed.
	static void FilterQuick(menus::Menu a_menu, const char* a_after)
	{
		if (a_menu == menus::Menu::kNone) return;
		FilterState& S = g_filter[static_cast<int>(a_menu)];
		if (!S.active) return;
		auto* list = reflect::Get(S.list);
		if (!list || ItemCount(list) == static_cast<int>(S.applied.size())) return;
		FilterTick(a_menu, Clock::now());
	}

	static void OnMenuPost(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
	{
		static bool s_in = false;   // our own calls into the list never come back here, but be sure
		if (s_in || !a_obj || !cfg::Get().ownFilter) return;
		auto* cls = a_obj->GetClass();
		if (cls && (cls == quests::PageClass() || cls == quests::PanelClass())) {   // the quests tab
					if (cls == quests::PageClass()) quests::NoteInstance(a_obj);
s_in = true;
					FilterQuick(menus::Menu::kQuests, pe::FunctionName(a_fn).c_str());
					s_in = false;
					return;
				}
				if (cls && cls == container::Class()) {   // the container / barter menu: both sides
			s_in = true;
			FilterQuick(menus::Menu::kContainerYours, pe::FunctionName(a_fn).c_str());
			FilterQuick(menus::Menu::kContainerTheirs, pe::FunctionName(a_fn).c_str());
			s_in = false;
			return;
		}
		const auto menu = cls == menus::ClassOf(menus::Menu::kMagic) ? menus::Menu::kMagic : cls == menus::ClassOf(menus::Menu::kInventory) ? menus::Menu::kInventory : menus::Menu::kNone;
		if (menu == menus::Menu::kNone) return;
		s_in = true;
		FilterQuick(menu, pe::FunctionName(a_fn).c_str());
		s_in = false;
	}

	static void RegisterMenuHooks()
	{
		static bool s_done[3] = { false, false, false };
		for (const auto m : { menus::Menu::kInventory, menus::Menu::kMagic }) {
			const int i = static_cast<int>(m);
			if (s_done[i]) continue;
			if (auto* cls = menus::ClassOf(m); cls && pe::WatchPost(cls, &OnMenuPost)) {
				s_done[i] = true;
				logger::debug("own filter: watching the {}'s calls to filter right after them", menus::Name(m));
			}
		}
		static bool s_doneQuestPage = false, s_doneQuestPanel = false;
				if (!s_doneQuestPage) {
					if (auto* cls = quests::PageClass(); cls && pe::WatchPost(cls, &OnMenuPost)) {
						s_doneQuestPage = true;
						logger::debug("own filter: watching the quests page's calls to filter right after them");
					}
				}
				if (!s_doneQuestPanel) {
					if (auto* cls = quests::PanelClass(); cls && pe::WatchPost(cls, &OnMenuPost)) {
						s_doneQuestPanel = true;
						logger::debug("own filter: watching the quests list panel's calls to filter right after them");
					}
				}
				static bool s_doneContainer = false;
		if (!s_doneContainer) {
			if (auto* cls = container::Class(); cls && pe::WatchPost(cls, &OnMenuPost)) {
				s_doneContainer = true;
				logger::debug("own filter: watching the container / barter menu's calls to filter right after them");
			}
		}
	}

	void BarTick(menus::Menu a_menu, Clock::time_point a_now)
	{
		if (!OwnBarOn() || !reflect::Ok()) return;
		InstallWndProc();
		if (!MenuEnabled(a_menu)) {
			if (g_typingMenu.load() == static_cast<int>(a_menu)) g_typing = false;
			if (IsSide(a_menu)) HideBar(a_menu);
			return;
		}
BarWidgets& B = g_bar[static_cast<int>(a_menu)];
const bool  have = reflect::Get(B.frame) && reflect::Get(B.label);
		if (!have || a_now >= B.nextLook) {   // built, and looked at twice a second: it must still sit in the menu's canvas (a menu that was rebuilt or replaced does not take it along)
			B.nextLook = a_now + 500ms;
			auto* canvas = BarCanvasFor(a_menu, !have);
			if (!canvas) {
				if (!have) return;
			} else if (!have || canvas != reflect::Get(B.canvas)) {
				if (have) logger::debug("own bar: the {}'s canvas is another one now - the bar is built again", menus::Name(a_menu));
				for (auto* w : { reflect::Get(B.frame), reflect::Get(B.edge), reflect::Get(B.label) }) {
					if (w) {
						ui::Call rm(w, L"RemoveFromParent");
						rm.Run();
					}
				}
				if (!BuildBar(a_menu, canvas)) return;
			}
		}
		UpdateBar(a_menu, a_now);
	}

	void Tick(menus::Menu a_menu)
	{
		static auto next = Clock::now();   // the engine lookups run here, on the game thread, about every 200 ms
		const auto  now = Clock::now();
		if (now >= next) {
			next = now + 200ms;
			if (!reflect::Ok()) reflect::SelfCheck();
			menus::Tick();
			container::Tick();
						quests::Tick();
}
		const bool cont = a_menu == menus::Menu::kNone && container::Open();   // the container / barter menu: a bar and a list on each side
		{   // which of its sides are on the screen: a plain container shows only its own list (nothing to give)
			static auto nextShown = Clock::now();
			if (!cont) {
				g_sideShown[static_cast<int>(menus::Menu::kContainerYours)] = false;
				g_sideShown[static_cast<int>(menus::Menu::kContainerTheirs)] = false;
			} else if (now >= nextShown) {
				nextShown = now + 100ms;
				for (const auto side : { menus::Menu::kContainerYours, menus::Menu::kContainerTheirs }) {
					const bool shown = SideShownNow(side);
					if (shown != g_sideShown[static_cast<int>(side)].load()) logger::debug("container menu: the {} is {}", menus::Name(side), shown ? "on the screen" : "not on the screen");
					g_sideShown[static_cast<int>(side)] = shown;
				}
			}
		}
const bool quest = !cont && a_menu == menus::Menu::kNone && quests::Open();   // the quests tab: a bar and the quest list
if (g_typing.load()) {
	const auto typed = static_cast<menus::Menu>(g_typingMenu.load());
	if ((typed != a_menu && !(cont && IsSide(typed)) && !(quest && typed == menus::Menu::kQuests)) || g_external.load()) g_typing = false;
}
		if (cont) {
			for (const auto side : { menus::Menu::kContainerYours, menus::Menu::kContainerTheirs }) {
				BarTick(side, now);
				FilterTick(side, now);
			}
		} else if (quest) {
			BarTick(menus::Menu::kQuests, now);
			FilterTick(menus::Menu::kQuests, now);
		} else {
			BarTick(a_menu, now);
			FilterTick(a_menu, now);
		}
}

	void SetGameThread(DWORD a_id) { g_gameThread = a_id; }
	void SetConflict(bool a_conflict) { g_conflict = a_conflict; }
	bool Enabled(menus::Menu a_menu) { return MenuEnabled(a_menu); }
	std::string GetText(menus::Menu a_menu) { return QueryOf(a_menu); }
	void SetText(menus::Menu a_menu, const std::string& a_text)
	{
		std::string s;
		for (const char c : a_text) {
			if (c >= 32 && c < 127 && s.size() < kMaxText) s.push_back(c);
		}
		SetQuery(a_menu, s);
	}
	bool PanelOnLeft(menus::Menu a_menu)
	{
		const auto& B = g_bar[static_cast<int>(a_menu) % 6];
		if (IsSide(a_menu) && B.w > 0) return B.x + B.w * 0.5 < g_canvasW * 0.5;
		return a_menu != menus::Menu::kContainerTheirs;
	}

	int Context()
{
		const auto m = menus::Active();
		if (m != menus::Menu::kNone) return static_cast<int>(m);
		if (!g_conflict && cfg::Get().container && container::Open()) return 3;
				return !g_conflict && cfg::Get().quests && quests::Open() ? 5 : 0;
	}
	void SetExternalInput(bool a_open)
{
		g_external = a_open;
		if (a_open) g_typing = false;
	}
}

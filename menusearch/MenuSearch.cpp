#include "MenuSearch.h"

#include "Config.h"
#include "PEHook.h"
#include "Reflect.h"
#include "Ui.h"
#include "UiText.h"

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
		std::string          g_queries[3];
		std::atomic<bool>    g_typing{ false };
		std::atomic<int>     g_typingMenu{ 0 };
		std::atomic<bool>    g_external{ false };   // the controller keyboard (another plugin) has the text: typed keys are not taken
		DWORD                g_gameThread = 0;

		bool MenuEnabled(menus::Menu a_menu)
		{
			if (g_conflict) return false;
			return (a_menu == menus::Menu::kInventory && cfg::Get().inventory) || (a_menu == menus::Menu::kMagic && cfg::Get().magic);
		}
		bool OwnBarOn() { return !g_conflict && (cfg::Get().inventory || cfg::Get().magic); }
		std::string QueryOf(menus::Menu a_menu)
		{
			std::scoped_lock l(g_qMutex);
			return g_queries[static_cast<int>(a_menu) % 3];
		}
		void SetQuery(menus::Menu a_menu, const std::string& a_text)
		{
			std::scoped_lock l(g_qMutex);
			g_queries[static_cast<int>(a_menu) % 3] = a_text;
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

		FilterState g_filter[3];   // by menu: kInventory, kMagic

		// ---- our own search bar (Inventory and Magic) ----
		// A bar in the menu's own root canvas, where the search mods put theirs, and the typing for it: while it is being typed into, the
		// game window's keys are taken here (the menu's hotkeys never see them), Ctrl+F / Ctrl+K or a click on the bar starts typing, Enter or
		// Esc ends it. The text lives in g_queries; the filter and the controller's keyboard read and write the same text.
		constexpr float kBarFill[4] = { 0.16f, 0.09f, 0.04f, 1.0f }, kBarEdge[4] = { 0.06f, 0.035f, 0.018f, 1.0f }, kBarFocus[4] = { 0.78f, 0.62f, 0.28f, 1.0f };
		constexpr float kBarInk[4] = { 0.93f, 0.90f, 0.82f, 1.0f }, kBarHint[4] = { 0.58f, 0.53f, 0.45f, 1.0f };

		struct BarWidgets
		{
			reflect::Handle   canvas, frame, edge, label;
			bool              textured = false;
			std::string       shown;
			bool              typing = false;
			Clock::time_point nextLook{}, nextMeasure{};
		};
		BarWidgets g_bar[3];
		struct PxRect { double x = 0, y = 0, w = 0, h = 0; bool valid = false; };
		PxRect  g_barPx[3];   // the bar's place in window pixels (g_qMutex)
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
			auto*        menuW = menus::Widget(a_menu);
			if (!menuW || !canvasClass || !treeClass || !reflect::Ok()) return nullptr;
			auto* wrapper = ui::FindInTree(menuW, a_menu == menus::Menu::kMagic ? "Magic_Spell_List" : "inv_cont_listview");
			auto* owner = wrapper ? OwnerWidget(wrapper) : nullptr;
			for (auto* w : { menuW, owner }) {
				if (!w || !w->GetClass()) continue;
				const auto wsOff = reflect::Offset(w->GetClass(), "WidgetTree");
				auto**      tree = wsOff >= 0 ? reflect::At<UE::UObject*>(w, wsOff) : nullptr;
				if (!tree || !*tree) continue;
				const auto rootOff = reflect::Offset(treeClass, "RootWidget");
				auto**      root = rootOff >= 0 ? reflect::At<UE::UObject*>(*tree, rootOff) : nullptr;
				if (root && *root && (*root)->GetClass() && (*root)->GetClass()->IsChildOf(canvasClass)) {
					if (a_log) logger::info("own bar: the {}'s bar goes into the root canvas of {}", menus::Name(a_menu), pe::Utf8(w->GetClass()->GetFName().ToString()));
					return *root;
				}
			}
			return nullptr;
		}

		// the bar's black, grainy panel: a PNG from the plugin's folder, else a plain rounded box
		UE::UObject* BarTexture()
		{
			static reflect::Handle s_tex;
			static bool            s_failed = false;
			if (auto* t = reflect::Get(s_tex)) return t;
			if (s_failed) return nullptr;
			const auto path = cfg::PluginFolder() / L"OblivionMenuSearch" / L"SearchBar.png";
			static auto* lib = ui::Class(L"/Script/Engine.KismetRenderingLibrary");
			ui::Call     c(lib ? lib->GetDefaultObject(false) : nullptr, L"ImportFileAsTexture2D");
			void*        file = c.At("Filename");
			UE::UObject* tex = nullptr;
			if (c && file && std::filesystem::exists(path)) {
				c.Set("WorldContextObject", ui::PlayerController());
				auto* s = new (file) UE::FString(path.wstring().c_str());
				tex = c.RunGuarded() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
				s->~FString();
			}
			if (!tex) {
				s_failed = true;
				logger::warn("own bar: the bar image could not be loaded ({}{}) - a plain box is used", path.string(), std::filesystem::exists(path) ? "" : ", missing");
				return nullptr;
			}
			s_tex = reflect::Hold(tex);
			logger::info("own bar: the bar image is loaded from {}", path.filename().string());
			return tex;
		}

		bool BuildBar(menus::Menu a_menu, UE::UObject* a_canvas)
		{
			static int s_n = 0;
			const std::wstring n = std::to_wstring(++s_n);
			const auto&       c = cfg::Get();
			UE::UObject*      slot = nullptr;
			auto*             frame = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", (L"IwmBarFrame" + n).c_str(), &slot);
			if (!frame || !slot) return false;
			const double barY = c.barY + (a_menu == menus::Menu::kMagic ? c.barYMagic : c.barYInventory);
			ui::Vec2(slot, L"SetPosition", c.barX, barY);
			ui::Vec2(slot, L"SetSize", c.barW, c.barH);
			const std::int32_t z = 1000;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			bool textured = false;
			if (auto* tex = BarTexture()) {
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
			ui::Vec2(lslot, L"SetPosition", c.barX + c.barW * 0.5, barY + c.barH * 0.5);
			const std::int32_t z2 = 1001;
			ui::CallFirst(lslot, L"SetZOrder", &z2, sizeof(z2));
			ui::Visible(label, true);
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			B.canvas = reflect::Hold(a_canvas);
			B.frame = reflect::Hold(frame);
			B.textured = textured;
			B.label = reflect::Hold(label);
			B.shown = "\x01";   // matches nothing real: the first update draws it
			B.typing = false;
			logger::info("own bar: built in the {}", menus::Name(a_menu));
			return true;
		}

		void UpdateBar(menus::Menu a_menu, Clock::time_point a_now)
		{
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			auto*       label = reflect::Get(B.label);
			auto*       frame = reflect::Get(B.frame);
			if (!label || !frame) return;
			const bool        typing = g_typing.load() && g_typingMenu.load() == static_cast<int>(a_menu);
			const std::string q = QueryOf(a_menu);
			const bool        caret = typing && (GetTickCount64() / 500) % 2 == 0;
			const bool        hint = q.empty() && !typing;
			const std::string show = hint ? std::string("Search") : q + (typing ? (caret ? "|" : " ") : "");
			const std::string key = std::string(hint ? "#" : "=") + show;
			if (key != B.shown) {
				uix::SetText(label, show);
				uix::Ink(label, hint ? kBarHint : kBarInk);
				uix::ApplyFont(label, cfg::Get().barFontSize);
				B.shown = key;
			}
			if (typing != B.typing) {   // the bronze edge while it is being typed into
				if (!B.textured) ui::RoundedBox(frame, false, kBarFill, typing ? kBarFocus : kBarEdge, 2.0f);
				B.typing = typing;
			}
			if (a_now >= B.nextMeasure) {   // where it is in window pixels, for a click on it
				B.nextMeasure = a_now + 150ms;
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
			auto&            q = g_queries[static_cast<int>(a_menu) % 3];
			if (a_c == 8) {
				if (!q.empty()) q.pop_back();
			} else if (a_c == 0x16) {   // Ctrl+V
				PasteInto(q);
			} else if (a_c >= 32 && a_c < 127 && q.size() < kMaxText) {
				q.push_back(static_cast<char>(a_c));
			}
		}

		bool InsideBar(menus::Menu a_menu, LPARAM a_l)
		{
			const int x = static_cast<short>(LOWORD(a_l)), y = static_cast<short>(HIWORD(a_l));
			std::scoped_lock l(g_qMutex);
			const auto& r = g_barPx[static_cast<int>(a_menu) % 3];
			return r.valid && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
		}

		bool ControllerKeyboardUp() { return g_external.load(); }

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
				const auto menu = menus::Active();
				if (MenuEnabled(menu)) {
					if (g_typing.load()) {
						switch (a_m) {
						case WM_KEYDOWN:   // a key typed into the bar: nothing else sees it (the menu's hotkeys stay quiet)
							if (a_w == VK_ESCAPE || a_w == VK_RETURN) g_typing = false;
							return 0;
						case WM_CHAR:
							Typed(static_cast<wchar_t>(a_w), menu);
							return 0;
						case WM_LBUTTONDOWN:
							if (!InsideBar(menu, a_l)) g_typing = false;   // a click elsewhere ends it and goes on to the game
							break;
						default: break;
						}
					} else if (!ControllerKeyboardUp()) {
						const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
						if (a_m == WM_KEYDOWN && (a_w == 'F' || a_w == 'K') && ctrl && !alt) {
							g_typingMenu = static_cast<int>(menu);
							g_typing = true;
							return 0;
						}
						if (a_m == WM_LBUTTONDOWN && InsideBar(menu, a_l)) {
							g_typingMenu = static_cast<int>(menu);
							g_typing = true;
							return 0;
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
			logger::info("own bar: the game window's keys and clicks are watched for typing ({})", g_origProc ? "ok" : "FAILED");
			if (g_origProc) {
				const DWORD windowThread = GetWindowThreadProcessId(found, nullptr);
				if (g_gameThread && windowThread == g_gameThread) {   // the window's messages are handled on the game's thread: a timer there is a safe extra tick
					SetTimer(found, kTimerId, 8, nullptr);
					logger::info("own bar: the game window is on the game thread ({}) - a timer on it ticks the search too", windowThread);
				} else {
					logger::info("own bar: the game window is on thread {}, the game thread is {} - the search is ticked by the controller read only", windowThread, g_gameThread);
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
		const bool supported = a_menu == menus::Menu::kMagic || a_menu == menus::Menu::kInventory;
		// a menu that is not the open one forgets everything it held
		for (int m = 1; m <= 2; ++m) {
			if ((!supported || m != static_cast<int>(a_menu)) && g_filter[m].watching) g_filter[m].Reset();
		}
		if (!cfg::Get().ownFilter || !reflect::Ok() || !MenuEnabled(a_menu)) return;
		FilterState& S = g_filter[static_cast<int>(a_menu)];
		const char* menuName = menus::Name(a_menu);
		if (!reflect::Get(S.list)) {   // the list, found a couple of times a second until it is
			if (a_now < S.nextLook) return;
			S.nextLook = a_now + 500ms;
			auto* menuW = menus::Widget(a_menu);
			auto* list = menuW ? ui::FindInTree(menuW, a_menu == menus::Menu::kMagic ? "Magic_Spell_List" : "inv_cont_listview") : nullptr;
			auto* view = list ? ListViewOf(list) : nullptr;
			if (!view) return;
			S.list = reflect::Hold(view);
			S.watching = true;
			logger::debug("own filter: watching the list of the {}", menuName);
		}
		auto* list = reflect::Get(S.list);
		if (!list) return;
		const auto text = QueryOf(a_menu);
		const auto query = Lower(text);
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
		if (a_menu != menus::Menu::kMagic && a_menu != menus::Menu::kInventory) return;
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
				logger::info("own filter: watching the {}'s calls to filter right after them", menus::Name(m));
			}
		}
	}

	void BarTick(menus::Menu a_menu, Clock::time_point a_now)
	{
		if (!OwnBarOn() || !reflect::Ok()) return;
		InstallWndProc();
		if (!MenuEnabled(a_menu)) {
			g_typing = false;
			return;
		}
		if (g_typing.load() && (g_typingMenu.load() != static_cast<int>(a_menu) || g_external.load())) g_typing = false;
		BarWidgets& B = g_bar[static_cast<int>(a_menu)];
		const bool have = reflect::Get(B.frame) && reflect::Get(B.label);
		if (!have || a_now >= B.nextLook) {   // built, and looked at twice a second: it must still sit in the menu's canvas (a menu that was rebuilt or replaced does not take it along)
			B.nextLook = a_now + 500ms;
			auto* canvas = BarCanvasFor(a_menu, !have);
			if (!canvas) {
				if (!have) return;
			} else if (!have || canvas != reflect::Get(B.canvas)) {
				if (have) logger::info("own bar: the {}'s canvas is another one now - the bar is built again", menus::Name(a_menu));
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
		}
		BarTick(a_menu, now);
		FilterTick(a_menu, now);
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
	void SetExternalInput(bool a_open)
	{
		g_external = a_open;
		if (a_open) g_typing = false;
	}
}

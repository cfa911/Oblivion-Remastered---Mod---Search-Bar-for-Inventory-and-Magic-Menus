#include "SearchKeyboard.h"

#include "Config.h"
#include "PEHook.h"
#include "Reflect.h"
#include "SearchApi.h"
#include "Ui.h"
#include "UiText.h"

#include <Xinput.h>   // constants only

#include <cstring>

namespace osk
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		enum class Kind { kChar, kSpace, kBack, kClear, kDone };
		struct Key
		{
			std::string label;
			char        ch;
			Kind        kind;
			int         row, col, span;   // ten columns; row 0-3 the characters, row 4 the four wide keys
		};

		constexpr int kCols = 10;
		constexpr int kRows = 4;

		const std::vector<Key>& Keys()
		{
			static const std::vector<Key> keys = [] {
				std::vector<Key> k;
				const std::array<const char*, 4> rows{ "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-[]", "1234567890" };
				for (int r = 0; r < 4; ++r) {
					for (int c = 0; c < kCols; ++c) {
						const char up = rows[static_cast<std::size_t>(r)][c];
						k.push_back({ std::string(1, up), static_cast<char>(std::tolower(static_cast<unsigned char>(up))), Kind::kChar, r, c, 1 });
					}
				}
				return k;
			}();
			return keys;
		}

		// colours are linear (the game's brushes), alpha last
		// the menus' parchment: the page behind the keyboard, darker keys on it, a gold selection, dark brown ink
		constexpr float kPanelFill[4] = { 0.76f, 0.69f, 0.55f, 1.0f }, kPanelEdge[4] = { 0.20f, 0.12f, 0.05f, 1.0f };
		constexpr float kKeyFill[4] = { 0.50f, 0.38f, 0.22f, 0.22f }, kKeyEdge[4] = { 0.25f, 0.15f, 0.06f, 0.55f };
		constexpr float kWideFill[4] = { 0.50f, 0.38f, 0.22f, 0.22f };
		constexpr float kSelFill[4] = { 0.74f, 0.60f, 0.42f, 0.90f }, kSelEdge[4] = { 0.22f, 0.13f, 0.06f, 1.0f };
		constexpr float kInk[4] = { 0.10f, 0.06f, 0.03f, 1.0f };
		constexpr float kGlyphFill[4] = { 0.12f, 0.11f, 0.10f, 1.0f }, kGlyphEdge[4] = { 0.72f, 0.64f, 0.52f, 1.0f }, kGlyphInk[4] = { 0.93f, 0.89f, 0.82f, 1.0f };

		reflect::Handle                                g_root, g_header;
		std::vector<reflect::Handle>                   g_keyImg;
		int                                            g_builds = 0;
		ULONGLONG                                      g_lastBuild = 0;

		bool                g_open = false;
		menus::Menu         g_menu = menus::Menu::kNone;
		int                 g_sel = 0, g_col = 0;
		std::string         g_text;
		WORD                g_ignore = 0;      // held at the opening: ignored until let go
		WORD                g_swallow = 0;     // held at the closing: the game must not see them until let go
		WORD                g_prevEff = 0;
		WORD                g_prevWant = 0;
		Clock::time_point   g_nextRepeat{}, g_xNext{};
		bool                g_xCleared = false;

		constexpr std::size_t kMaxText = 40;


		void ZeroOffsets(UE::UObject* a_slot)
		{
			ui::Call c(a_slot, L"SetOffsets");   // all zero: with stretching anchors the slot fills the anchored rectangle
			c.Run();
		}

		double Aspect()
		{
			RECT  r{};
			HWND  w = GetForegroundWindow();
			DWORD pid = 0;
			if (w) GetWindowThreadProcessId(w, &pid);
			if (w && pid == GetCurrentProcessId() && GetClientRect(w, &r) && r.bottom > 0 && r.right > 0) return static_cast<double>(r.right) / static_cast<double>(r.bottom);
			return 16.0 / 9.0;
		}

		struct Rect { double x0, y0, x1, y1; };

		// every key's place as a share of the screen; the panel and the header / hint rows too
		struct Layout
		{
			Rect panel{}, header{}, hint{};
			std::vector<Rect> key;
		};

		Layout MakeLayout()
		{
			const auto&  s = cfg::Get();
			const double L = s.leftPercent / 100.0, T = s.topPercent / 100.0, R = s.rightPercent / 100.0, B = s.bottomPercent / 100.0;
			const double W = R - L, H = B - T;
			const double padX = W * 0.03;
			const double colW = (W - 2 * padX) / kCols;
			const double rowH = std::min(H / (kRows + 2 + 0.7), colW * Aspect() * 1.25);   // header, the key rows, the hint; keys a little taller than wide
			const double padY = (H - (kRows + 2) * rowH) * 0.5;   // the rest of the height is margin, above and below
			Layout       l;
			const double x0 = L + padX, y0 = T + padY;
			l.panel = { L, T, R, T + 2 * padY + (kRows + 2) * rowH };
			l.header = { x0, y0, x0 + kCols * colW, y0 + rowH };
			l.hint = { x0, y0 + (kRows + 1) * rowH, x0 + kCols * colW, y0 + (kRows + 2) * rowH };
			const double gx = colW * 0.07, gy = rowH * 0.08;
			for (const auto& k : Keys()) {
				const double kx = x0 + k.col * colW, ky = y0 + (k.row + 1) * rowH;
				l.key.push_back({ kx + gx, ky + gy, kx + k.span * colW - gx, ky + rowH - gy });
			}
			return l;
		}

		UE::UObject* AddImage(UE::UObject* a_canvas, const std::wstring& a_name, const Rect& a_r, const float a_fill[4], const float a_edge[4], float a_width, bool a_circle = false)
		{
			UE::UObject* slot = nullptr;
			auto*        img = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", a_name.c_str(), &slot);
			if (!img || !slot) return nullptr;
			ui::Anchors(slot, a_r.x0, a_r.y0, a_r.x1, a_r.y1);
			ZeroOffsets(slot);
			if (a_fill && !ui::RoundedBox(img, a_circle, a_fill, a_edge, a_width)) {
				logger::warn("search keyboard: a rounded box could not be set on {}", std::string(a_name.begin(), a_name.end()));
			}
			return img;
		}


		double g_headerSize = 32.0;

		UE::UObject* AddLabel(UE::UObject* a_canvas, const std::wstring& a_name, const Rect& a_r, double a_size, const std::string& a_text, const float* a_ink = kInk, double a_alignX = 0.5)
		{
			UE::UObject* slot = nullptr;
			auto*        label = ui::AddToCanvas(a_canvas, L"/Script/UMG.TextBlock", a_name.c_str(), &slot);
			if (!label || !slot) return nullptr;
			const double ax = a_alignX < 0.25 ? a_r.x0 : (a_r.x0 + a_r.x1) * 0.5;
			ui::Anchors(slot, ax, (a_r.y0 + a_r.y1) * 0.5, ax, (a_r.y0 + a_r.y1) * 0.5);
			ui::Vec2(slot, L"SetAlignment", a_alignX, 0.5);
			const bool yes = true;
			ui::CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
			ui::Vec2(slot, L"SetPosition", 0.0, 0.0);
			ui::Vec2(label, L"SetRenderTransformPivot", 0.5, 0.5);
			uix::SetText(label, a_text);
			uix::Ink(label, a_ink);
			if (!uix::ApplyFont(label, a_size)) {
				const double scale = a_size / 24.0;   // no font to set: the prefab's own, scaled instead
				ui::Vec2(label, L"SetRenderScale", scale, scale);
			}
			return label;
		}

		// ours, from the plugin's folder: a texture the engine imports from the PNG (KismetRenderingLibrary), as the wheel mod's star does
		UE::UObject* ParchmentTexture()
		{
			static reflect::Handle s_tex;
			static bool            s_failed = false;
			if (auto* t = reflect::Get(s_tex)) return t;
			if (s_failed) return nullptr;
			const auto path = cfg::PluginFolder() / L"OblivionSearchKeyboard" / L"Parchment.png";
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
				logger::warn("search keyboard: the parchment image could not be loaded ({}{}) - a plain box is used", path.string(), std::filesystem::exists(path) ? "" : ", missing");
				return nullptr;
			}
			s_tex = reflect::Hold(tex);
			logger::info("search keyboard: the parchment is loaded from {}", path.filename().string());
			return tex;
		}

		bool Build()
		{
			const ULONGLONG now = GetTickCount64();
			if (g_lastBuild && now - g_lastBuild < 2000) return false;
			g_lastBuild = now;
			auto*              root = ui::CreateWidget(L"/Script/UMG.UserWidget");
			const std::wstring n = std::to_wstring(++g_builds);
			auto*              canvas = root ? ui::RootCanvas(root, (L"IwmOskCanvas" + n).c_str()) : nullptr;
			if (!canvas) {
				logger::info("search keyboard: the widget could not be created yet (build {})", g_builds);
				return false;
			}
			const auto   layout = MakeLayout();
			const double scale = cfg::Get().textPercent / 100.0;
			{   // the page: a parchment image from the plugin's folder, else a plain rounded box
				auto* tex = ParchmentTexture();
				auto* panel = AddImage(canvas, L"IwmOskPanel" + n, layout.panel, tex ? nullptr : kPanelFill, kPanelEdge, 3.0f);
				if (!panel) return false;
				if (tex) {
					ui::Call b(panel, L"SetBrushFromTexture");
					b.Set("Texture", tex);
					b.Set("bMatchSize", false);
					b.Run();
				}
			}
			std::vector<reflect::Handle> imgs;
			const auto&                  keys = Keys();
			for (std::size_t i = 0; i < keys.size(); ++i) {
				auto* img = AddImage(canvas, L"IwmOskKey" + n + L"_" + std::to_wstring(i), layout.key[i],
					keys[i].kind == Kind::kChar ? kKeyFill : kWideFill, kKeyEdge, 1.5f);
				if (!img) return false;
				imgs.push_back(reflect::Hold(img));
			}
			// the labels go over every image (a later child draws above an earlier one)
			for (std::size_t i = 0; i < keys.size(); ++i) {
				if (!AddLabel(canvas, L"IwmOskText" + n + L"_" + std::to_wstring(i), layout.key[i], scale * (keys[i].kind == Kind::kChar ? 36.0 : 28.0), keys[i].label)) {
					logger::info("search keyboard: the game's text prefab is not loaded yet");
					return false;
				}
			}
			g_headerSize = scale * 34.0;
				auto* header = AddLabel(canvas, L"IwmOskHeader" + n, layout.header, g_headerSize, "Search: _");
			{   // the pad's buttons as the game draws them: a dark disc with a light ring and the letter, then what the button does
				struct Hint { const char* glyph; const char* text; };
				const std::array<Hint, 4> hints{ { { "A", "Type" }, { "X", "Delete (hold: clear)" }, { "Y", "Space" }, { "B", "Done" } } };
				// sizes at full scale; the whole row is then shrunk to fit the page if it is wider than it
				const double cy = (layout.hint.y0 + layout.hint.y1) * 0.5;
				const double textFull = scale * 22.0;
				const double dhFull = (layout.hint.y1 - layout.hint.y0) * 0.55;   // the disc's height, a share of the screen's height
				const double charFull = 0.00675;                                   // an average character's width as a share of the screen's width at size 22 (measured in game)
				const double dwFull = dhFull / Aspect();                           // the same size across, so it is a circle
				double       total1 = 0;
				for (const auto& h : hints) total1 += dwFull * 1.3 + std::strlen(h.text) * charFull * (textFull / 22.0);
				total1 += dwFull * 0.9 * (hints.size() - 1);
				const double fit = std::min(1.0, (layout.hint.x1 - layout.hint.x0) * 0.94 / total1);
				const double textSize = textFull * fit;
				const double charW = charFull * (textSize / 22.0);
				const double dh = dhFull * fit;
				const double dw = dwFull * fit;
				const double gap = dw * 0.3, between = dw * 0.9;
				double       total = 0;
				for (const auto& h : hints) total += dw + gap + std::strlen(h.text) * charW;
				total += between * (hints.size() - 1);
				double x = (layout.hint.x0 + layout.hint.x1) * 0.5 - total * 0.5;
				for (std::size_t i = 0; i < hints.size(); ++i) {
					const Rect disc{ x, cy - dh * 0.5, x + dw, cy + dh * 0.5 };
					const std::wstring id = n + L"_" + std::to_wstring(i);
					{   // the game's own button icon (Art/UI/Icons/Icons_Inputs/Xbox, the set its prompts use); a drawn disc and letter when it cannot be loaded
						const std::wstring tn = std::wstring(L"T_Icon_Xbox_") + static_cast<wchar_t>(hints[i].glyph[0]);
						auto*              tex = ui::Load((L"/Game/Art/UI/Icons/Icons_Inputs/Xbox/" + tn + L"." + tn).c_str());
						static bool        s_said = false;
						if (!s_said) {
							s_said = true;
							logger::info("search keyboard: the game's button icons {}", tex ? "are loaded" : "could NOT be loaded - drawn discs are used");
						}
						if (tex) {
							auto* icon = AddImage(canvas, L"IwmOskGlyph" + id, disc, nullptr, kGlyphEdge, 0.0f);
							if (!icon) return false;
							ui::Call b(icon, L"SetBrushFromTexture");
							b.Set("Texture", tex);
							b.Set("bMatchSize", false);
							b.Run();
						} else {
							if (!AddImage(canvas, L"IwmOskGlyph" + id, disc, kGlyphFill, kGlyphEdge, 2.5f, true)) return false;
							AddLabel(canvas, L"IwmOskGlyphText" + id, disc, textSize * 0.9, hints[i].glyph, kGlyphInk);
						}
					}
					const double tx = x + dw + gap;
					AddLabel(canvas, L"IwmOskHint" + id, Rect{ tx, disc.y0, tx + 0.05, disc.y1 }, textSize, hints[i].text, kInk, 0.0);
					x += dw + gap + std::strlen(hints[i].text) * charW + between;
				}
			}
			if (!header) return false;
			ui::Call vp(root, L"AddToViewport");
			vp.Set<std::int32_t>("ZOrder", 65);   // over the wheel's labels (60, 61)
			vp.Run();
			ui::Visible(root, false);
			g_root = reflect::Hold(root);
			g_header = reflect::Hold(header);
			g_keyImg = std::move(imgs);
			logger::info("search keyboard: built (build {}), panel {:.0f}%,{:.0f}% to {:.0f}%,{:.0f}% of the screen", g_builds, layout.panel.x0 * 100, layout.panel.y0 * 100,
				layout.panel.x1 * 100, layout.panel.y1 * 100);
			return true;
		}

		bool EnsureBuilt()
		{
			auto* root = reflect::Get(g_root);
			if (root && reflect::Get(g_header) && !g_keyImg.empty()) {
				ui::Call in(root, L"IsInViewport");
				if (in && in.Run() && !in.Get<bool>("ReturnValue")) {   // a world change took it off the screen
					ui::Call vp(root, L"AddToViewport");
					vp.Set<std::int32_t>("ZOrder", 65);
					vp.Run();
				}
				return true;
			}
			g_root = {};
			g_header = {};
			g_keyImg.clear();
			return Build();
		}

		void Paint(int a_index, bool a_selected)
		{
			const auto& keys = Keys();
			if (a_index < 0 || a_index >= static_cast<int>(g_keyImg.size())) return;
			auto* img = reflect::Get(g_keyImg[static_cast<std::size_t>(a_index)]);
			if (!img) return;
			const bool kc = keys[static_cast<std::size_t>(a_index)].kind == Kind::kChar;
			ui::RoundedBox(img, false, a_selected ? kSelFill : kc ? kKeyFill : kWideFill, a_selected ? kSelEdge : kKeyEdge, a_selected ? 3.0f : 1.5f);
		}

		void ShowText()
		{
			if (auto* h = reflect::Get(g_header)) {
				uix::SetText(h, "Search: " + g_text + "_");
				uix::Ink(h, kInk);
			uix::ApplyFont(h, g_headerSize);
			}
		}

		// the box's own text: the other plugin filters on it
		void Push()
		{
			search::SetText(g_menu, g_text);
			ShowText();
		}

		int KeyAt(int a_row, int a_col)
		{
			const auto& keys = Keys();
			for (std::size_t i = 0; i < keys.size(); ++i) {
				if (keys[i].row == a_row && a_col >= keys[i].col && a_col < keys[i].col + keys[i].span) return static_cast<int>(i);
			}
			return 0;
		}

		void Select(int a_index)
		{
			if (a_index == g_sel) return;
			Paint(g_sel, false);
			g_sel = a_index;
			Paint(g_sel, true);
		}

		enum : WORD { kUp = 1, kDown = 2, kLeft = 4, kRight = 8 };

		void Move(WORD a_dir)
		{
			const auto& k = Keys()[static_cast<std::size_t>(g_sel)];
			if (a_dir & (kLeft | kRight)) {
				int c = (a_dir & kLeft) ? k.col - 1 : k.col + k.span;
				if (c < 0) c = kCols - 1;
				if (c >= kCols) c = 0;
				const int n = KeyAt(k.row, c);
				g_col = Keys()[static_cast<std::size_t>(n)].col;
				Select(n);
			} else {
				int r = k.row + ((a_dir & kUp) ? -1 : 1);
				if (r < 0) r = kRows - 1;
				if (r >= kRows) r = 0;
				Select(KeyAt(r, g_col));
			}
		}

		void Backspace()
		{
			if (g_text.empty()) return;
			g_text.pop_back();
			Push();
		}

		void Press(int a_index)
		{
			const auto& k = Keys()[static_cast<std::size_t>(a_index)];
			switch (k.kind) {
			case Kind::kChar:
			case Kind::kSpace:
				if (g_text.size() < kMaxText) {
					g_text.push_back(k.ch);
					Push();
				}
				break;
			case Kind::kBack: Backspace(); break;
			case Kind::kClear:
				g_text.clear();
				Push();
				break;
			case Kind::kDone: break;   // handled by the caller (it closes)
			}
		}
	}

	bool Active() { return g_open; }
	bool Taking() { return g_open; }

	bool CanOpen(menus::Menu a_menu)
	{
		return cfg::Get().enabled && reflect::Ok() && search::Enabled(a_menu);
	}

	bool Open(menus::Menu a_menu, std::uint16_t a_held)
	{
		if (g_open) return true;
		if (!search::Enabled(a_menu) || !EnsureBuilt()) return false;
		g_text = search::GetText(a_menu);
		search::SetExternalInput(true);   // the search bar stops taking typed keys while the keyboard has the text
		if (g_text.size() > kMaxText) g_text.resize(kMaxText);
		g_menu = a_menu;
		for (int i = 0; i < static_cast<int>(g_keyImg.size()); ++i) Paint(i, false);   // no key keeps the highlight of the last time it was open
		g_sel = -1;
		Select(KeyAt(0, 0));   // Q
		g_col = 0;
		g_ignore = a_held;
		g_prevEff = 0;
		g_prevWant = 0;
		g_swallow = 0;
		ShowText();
		ui::Visible(reflect::Get(g_root), true);
		g_open = true;
		logger::debug("search keyboard: opened in the {} with \"{}\"", menus::Name(a_menu), g_text);
		return true;
	}

	void Close(const char* a_why, std::uint16_t a_held)
	{
		if (!g_open) return;
		g_open = false;
		search::SetExternalInput(false);
		g_swallow = a_held;
		ui::Visible(reflect::Get(g_root), false);
		logger::debug("search keyboard: closed ({}) with \"{}\"", a_why, g_text);
	}

	std::uint16_t Swallow(std::uint16_t a_raw)
	{
		g_swallow &= a_raw;
		return g_swallow;
	}

	void Rewrite(std::uint16_t a_raw, std::int16_t a_lx, std::int16_t a_ly, Clock::time_point a_now)
	{
		if (!g_open) return;
		g_ignore &= a_raw;
		const WORD raw = a_raw & static_cast<WORD>(~g_ignore);
		const WORD pressed = raw & static_cast<WORD>(~g_prevEff);
		g_prevEff = raw;

		// the menu went away
		if (menus::Active() != g_menu) {
			Close("menu changed");
			return;
		}
		if (pressed & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START)) {
			Close("B", a_raw);
			return;
		}

		// direction: the D-pad, else the left stick's stronger axis
		WORD want = 0;
		if (raw & XINPUT_GAMEPAD_DPAD_UP) want = kUp;
		else if (raw & XINPUT_GAMEPAD_DPAD_DOWN) want = kDown;
		else if (raw & XINPUT_GAMEPAD_DPAD_LEFT) want = kLeft;
		else if (raw & XINPUT_GAMEPAD_DPAD_RIGHT) want = kRight;
		else if (std::abs(a_lx) > 16000 || std::abs(a_ly) > 16000) {
			if (std::abs(a_lx) >= std::abs(a_ly)) want = a_lx < 0 ? kLeft : kRight;
			else want = a_ly > 0 ? kUp : kDown;
		}
		if (want != g_prevWant) {
			g_prevWant = want;
			if (want) {
				Move(want);
				g_nextRepeat = a_now + 350ms;
			}
		} else if (want && a_now >= g_nextRepeat) {
			Move(want);
			g_nextRepeat = a_now + 110ms;
		}

		if (pressed & XINPUT_GAMEPAD_A) {
			if (Keys()[static_cast<std::size_t>(g_sel)].kind == Kind::kDone) {
				Close("Done", a_raw);
				return;
			}
			Press(g_sel);
		}
		if (pressed & XINPUT_GAMEPAD_Y) {
			if (g_text.size() < kMaxText) {
				g_text.push_back(' ');
				Push();
			}
		}
		if (raw & XINPUT_GAMEPAD_X) {   // X deletes a character; held for 0.7 s it clears the whole text
			if (pressed & XINPUT_GAMEPAD_X) {
				Backspace();
				g_xNext = a_now + 700ms;
				g_xCleared = false;
			} else if (!g_xCleared && a_now >= g_xNext) {
				g_xCleared = true;
				g_text.clear();
				Push();
			}
		}
	}
}

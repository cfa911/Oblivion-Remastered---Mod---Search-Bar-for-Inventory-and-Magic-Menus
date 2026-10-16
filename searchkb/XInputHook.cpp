#include "XInputHook.h"

#include "Config.h"
#include "Menus.h"
#include "Reflect.h"
#include "SearchKeyboard.h"

#include <Xinput.h>   // types and constants only - nothing is linked or loaded

namespace hook
{
	namespace
	{
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		using Clock = std::chrono::steady_clock;

		constexpr int kPulseReads = 4;   // a replayed tap is held for this many reads, then let go

		XInputGetState_t  g_previous = nullptr;   // what the game's import pointed at before us
		std::atomic<bool> g_installed{ false };
		DWORD             g_gameThread = 0;

		// ---- state, touched only inside the read (the game thread) ----
		WORD        g_prevRaw = 0;
		WORD        g_prevOutButtons = 0;
		BYTE        g_prevOutLT = 0, g_prevOutRT = 0;
		SHORT       g_prevOutRX = 0, g_prevOutRY = 0;
		DWORD       g_packetOffset = 0;
		menus::Menu g_prevMenu = menus::Menu::kNone;

		bool              g_held = false;    // the keyboard's button went down in a menu with a search box
		bool              g_fired = false;   // ... and the keyboard opened for it
		Clock::time_point g_at{};
		int               g_pulse = 0;       // reads left of a replayed tap

		// the engine lookups run here, on the game thread, about every 200 ms (never from a thread of our own: a start-up
		// crashed in UObjectArray that way, 2026-09-29)
		void Tick()
		{
			static auto next = Clock::now();
			const auto  now = Clock::now();
			if (now < next) return;
			next = now + 200ms;
			if (!reflect::Ok()) reflect::SelfCheck();
			menus::Tick();
		}

		void Rewrite(XINPUT_GAMEPAD& a_pad)
		{
			const WORD raw = a_pad.wButtons;
			const WORD pressed = raw & static_cast<WORD>(~g_prevRaw);
			g_prevRaw = raw;
			const auto now = Clock::now();
			const auto menu = menus::Active();
			const auto& c = cfg::Get();

			if (menu != g_prevMenu) {   // a menu opened or closed (or the tab changed): the keyboard goes with it, a half-timed hold is forgotten
				g_held = false;
				g_fired = false;
				osk::Close("menu changed");
				g_prevMenu = menu;
			}


			// the keyboard has the pad: the menu stands still, the game sees an idle controller
			if (osk::Taking()) {
				osk::Rewrite(raw, a_pad.sThumbLX, a_pad.sThumbLY, now);
				a_pad.wButtons = 0;
				a_pad.bLeftTrigger = 0;
				a_pad.bRightTrigger = 0;
				a_pad.sThumbLX = a_pad.sThumbLY = a_pad.sThumbRX = a_pad.sThumbRY = 0;
				return;
			}

			WORD out = raw;
			out &= static_cast<WORD>(~osk::Swallow(raw));   // held when the keyboard closed (B, Start...): the game never sees them

			// A hold on the button opens the keyboard; a tap is replayed after the release, so the game's own use of the button
			// (stepping the sort) is kept.
			if (c.enabled && c.button && menu != menus::Menu::kNone) {
				const WORD btn = static_cast<WORD>(c.button);
				if (pressed & btn) {
					g_fired = false;
					g_at = now;
					g_held = osk::CanOpen(menu);   // the keyboard cannot open here (no Menu Search plugin, or its search is off in this menu): the game keeps the button
				}
				if (g_held) {
					out &= static_cast<WORD>(~btn);
					if (!g_fired && (raw & btn) && now - g_at >= std::chrono::milliseconds(c.holdMs)) {
						g_fired = true;
						osk::Open(menu, raw);
					}
					if (!(raw & btn)) {
						if (!g_fired) g_pulse = kPulseReads;   // a tap: the game gets it now
						g_held = false;
					}
				}
			}
			if (g_pulse > 0) {
				out |= static_cast<WORD>(c.button);
				--g_pulse;
			}
			a_pad.wButtons = out;
		}

		DWORD WINAPI Chained(DWORD a_user, XINPUT_STATE* a_state)
		{
			DWORD rc = g_previous ? g_previous(a_user, a_state) : ERROR_DEVICE_NOT_CONNECTED;
			// only the game thread's reads of controller 0 are touched (another thread reading through the import is passed through)
			if (a_user != 0 || !a_state || rc != ERROR_SUCCESS || GetCurrentThreadId() != g_gameThread) {
				return rc;
			}
			Tick();
			Rewrite(a_state->Gamepad);
			const auto& g = a_state->Gamepad;
			// the game only processes a state whose packet number moved: move it whenever what we hand over changes
			if (g.wButtons != g_prevOutButtons || g.bLeftTrigger != g_prevOutLT || g.bRightTrigger != g_prevOutRT || g.sThumbRX != g_prevOutRX || g.sThumbRY != g_prevOutRY) {
				++g_packetOffset;
				g_prevOutButtons = g.wButtons;
				g_prevOutLT = g.bLeftTrigger;
				g_prevOutRT = g.bRightTrigger;
				g_prevOutRX = g.sThumbRX;
				g_prevOutRY = g.sThumbRY;
			}
			a_state->dwPacketNumber += g_packetOffset;
			return rc;
		}

		std::string ModuleOf(const void* a_p)
		{
			HMODULE m = nullptr;
			wchar_t w[MAX_PATH]{};
			if (a_p && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(a_p), &m) && m &&
				GetModuleFileNameW(m, w, MAX_PATH)) {
				return std::filesystem::path(w).filename().string();
			}
			return "an unknown module";
		}
	}

	bool Install()
	{
		if (g_installed.load()) return true;
		static bool s_tried = false;
		if (s_tried) return false;
		s_tried = true;
		g_gameThread = GetCurrentThreadId();   // Install runs at OBSE's post-load, on the game's main thread
		auto*       base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!dir.VirtualAddress) {
			logger::error("hook: the game has no import table - no controller support");
			return false;
		}
		for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
			const char* dll = reinterpret_cast<const char*>(base + desc->Name);
			if (_strnicmp(dll, "xinput", 6) != 0) continue;
			auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
			auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
			for (; names->u1.AddressOfData; ++names, ++slots) {
				const bool match = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
				                       ? IMAGE_ORDINAL64(names->u1.Ordinal) == 2
				                       : std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, "XInputGetState") == 0;
				if (!match) continue;
				auto* slot = reinterpret_cast<XInputGetState_t*>(&slots->u1.Function);
				DWORD old = 0;
				if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
					logger::error("hook: the import slot could not be made writable ({})", GetLastError());
					return false;
				}
				g_previous = *slot;
				*slot = &Chained;
				VirtualProtect(slot, sizeof(void*), old, &old);
				g_installed.store(true);
				logger::info("hook: the game's {} XInputGetState import now runs the search keyboard (previously in {})", dll, ModuleOf(reinterpret_cast<const void*>(g_previous)));
				return true;
			}
		}
		logger::error("hook: the game imports no XInputGetState - no controller support");
		return false;
	}
}

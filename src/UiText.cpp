#include "UiText.h"

#include "PEHook.h"
#include "Reflect.h"
#include "TextUtil.h"
#include "Ui.h"

namespace uix
{
	namespace
	{
		constexpr const wchar_t* kFontAsset = L"/Game/UI/CommonStyle/Fonts/Font_Robinson_Localized.Font_Robinson_Localized";
		// the engine's default font: Roboto with Droid Sans Fallback behind it (both are in the game's paks), which draws Greek, Cyrillic, kana, Han and Hangul
		constexpr const wchar_t* kFallbackFontAsset = L"/Engine/EngineFonts/Roboto.Roboto";

		// the game's current culture ("de", "ja", "zh-Hans" ...), read once it is known; empty if it cannot be read
		const std::string& Culture()
		{
			static std::string s_culture;
			static bool        s_tried = false;
			if (s_tried) return s_culture;
			static auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Engine.KismetInternationalizationLibrary");
			auto*        cdo = cls ? cls->GetDefaultObject(false) : nullptr;
			ui::Call     c(cdo, L"GetCurrentCulture");
			void*        ret = c ? c.At("ReturnValue") : nullptr;
			if (!ret || c.Size("ReturnValue") != static_cast<std::int32_t>(sizeof(UE::FString)) || !c.Run()) return s_culture;   // tried again next time
			auto* s = static_cast<UE::FString*>(ret);
			s_culture = pe::Utf8(*s);
			if (void* data = *reinterpret_cast<void**>(s)) UE::FMemory::Free(data);   // the engine allocated the characters
			s_tried = true;
			logger::info("uix: the game's culture is \"{}\"", s_culture);
			return s_culture;
		}
	}

	bool NeedsFallbackFont(const std::string& a_text)
	{
		if (!textutil::NeedsWideFont(a_text)) return false;
		// in a Japanese or Chinese game the localized font carries the culture's own faces (Noto Serif JP, Yuji Syuku, Noto Serif SC): it draws their text itself
		const auto& culture = Culture();
		return !(culture.starts_with("ja") || culture.starts_with("zh"));
	}

	void Ink(UE::UObject* a_label, const float* a_c)
	{
		struct SlateColor
		{
			float        rgba[4];
			std::uint8_t rule;
			std::uint8_t pad[7];
		} ink{ { a_c[0], a_c[1], a_c[2], a_c[3] }, 0, {} };
		ui::CallFirst(a_label, L"SetColor", &ink, sizeof(ink));
		ui::CallFirst(a_label, L"SetColorAndOpacity", &ink, sizeof(ink));
		const float shadow[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // none
		ui::CallFirst(a_label, L"SetShadowColorAndOpacity", shadow, sizeof(shadow));
	}

	bool SetText(UE::UObject* a_widget, const std::string& a_text)
	{
		auto*      fn = a_widget ? a_widget->FindFunction(UE::FName(L"SetText", UE::EFindName::Find)) : nullptr;
		const auto params = fn ? reflect::Fields(reinterpret_cast<UE::UStruct*>(fn)) : std::vector<std::pair<std::string, std::int32_t>>{};
		if (params.empty()) return false;
		std::vector<std::uint8_t> buf(static_cast<std::size_t>(std::max(reinterpret_cast<UE::UStruct*>(fn)->propertiesSize, 0)) + 16, 0);
		const std::wstring w = textutil::Wide(a_text);   // UTF-8: a byte-by-byte widening turned "é" into two wrong characters
		auto*              text = new (buf.data() + params.front().second) UE::FText(UE::FText::AsCultureInvariant(UE::FString(w.c_str())));
		a_widget->ProcessEvent(fn, buf.data());
		text->~FText();
		return true;
	}

	std::string GetText(UE::UObject* a_widget)
	{
		ui::Call c(a_widget, L"GetText");
		void*    ret = c ? c.At("ReturnValue") : nullptr;
		if (!ret || !c.Run()) return {};
		auto*       ft = static_cast<UE::FText*>(ret);
		std::string out = reflect::Text(*ft);
		ft->~FText();
		return out;
	}

	namespace
	{
		bool Apply(UE::UObject* a_w, double a_size, const float* a_outline, int a_outlineSize = 1, bool a_fallback = false)
		{
		static auto*           fontInfo = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo"));
		static reflect::Handle s_font, s_fallbackFont;
		auto&                  slot = a_fallback ? s_fallbackFont : s_font;
		auto*                  font = reflect::Get(slot);
		if (!font) {
			font = ui::Load(a_fallback ? kFallbackFontAsset : kFontAsset);
			if (font) slot = reflect::Hold(font);
		}
		if (!a_w || !a_w->GetClass() || !fontInfo || !font || !reflect::Ok()) return false;
		const auto propOff = reflect::Offset(a_w->GetClass(), "Font");
		const auto propSize = ui::SizeOf(a_w->GetClass(), "Font");
		const auto objOff = reflect::Offset(fontInfo, "FontObject");
		const auto sizeOff = reflect::Offset(fontInfo, "Size");
		const auto sizeSize = ui::SizeOf(fontInfo, "Size");
		if (propOff < 0 || propSize <= 0 || objOff < 0 || sizeOff < 0 || (sizeSize != 4 && sizeSize != 8)) return false;
		auto* b = reinterpret_cast<std::uint8_t*>(a_w) + propOff;
		*reinterpret_cast<UE::UObject**>(b + objOff) = font;
		if (a_size > 0) {
			if (sizeSize == 4) *reinterpret_cast<float*>(b + sizeOff) = static_cast<float>(a_size);
			else *reinterpret_cast<double*>(b + sizeOff) = a_size;
		}
		if (a_outline) {   // FSlateFontInfo::OutlineSettings { OutlineSize (int32 in UE 5.3), OutlineColor }
			static auto* outline = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.FontOutlineSettings"));
			const auto   osOff = reflect::Offset(fontInfo, "OutlineSettings");
			const auto   sizeOff2 = outline ? reflect::Offset(outline, "OutlineSize") : -1;
			const auto   colOff = outline ? reflect::Offset(outline, "OutlineColor") : -1;
			if (osOff >= 0 && sizeOff2 >= 0 && colOff >= 0 && ui::SizeOf(outline, "OutlineSize") == 4) {
				*reinterpret_cast<std::int32_t*>(b + osOff + sizeOff2) = a_outlineSize;
				std::memcpy(b + osOff + colOff, a_outline, 4 * sizeof(float));
			}
		}
		std::vector<std::uint8_t> copy(b, b + propSize);
		return ui::CallFirst(a_w, L"SetFont", copy.data(), copy.size());
		}
	}

	bool ApplyFont(UE::UObject* a_w, double a_size, bool a_fallback)
	{
		return Apply(a_w, a_size, nullptr, 1, a_fallback);
	}

	bool ApplyBoldFont(UE::UObject* a_w, double a_size, const float* a_rgba, int a_outlineSize)
	{
		return Apply(a_w, a_size, a_rgba, a_outlineSize);
	}


	bool ApplyPrefabFont(UE::UObject* a_label)
	{
		auto* inner = a_label ? ui::FindInTree(a_label, "text_block") : nullptr;
		return inner && ApplyFont(inner, 0);
	}
}

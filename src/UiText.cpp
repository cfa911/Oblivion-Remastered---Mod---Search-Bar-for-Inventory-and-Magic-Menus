#include "UiText.h"

#include "Reflect.h"
#include "Ui.h"

namespace uix
{
	namespace
	{
		constexpr const wchar_t* kFontAsset = L"/Game/UI/CommonStyle/Fonts/Font_Robinson_Localized.Font_Robinson_Localized";
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
		const std::wstring w(a_text.begin(), a_text.end());
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

	bool ApplyFont(UE::UObject* a_w, double a_size)
	{
		static auto*           fontInfo = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo"));
		static reflect::Handle s_font;
		auto*                  font = reflect::Get(s_font);
		if (!font) {
			font = ui::Load(kFontAsset);
			if (font) s_font = reflect::Hold(font);
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
		if (sizeSize == 4) *reinterpret_cast<float*>(b + sizeOff) = static_cast<float>(a_size);
		else *reinterpret_cast<double*>(b + sizeOff) = a_size;
		std::vector<std::uint8_t> copy(b, b + propSize);
		return ui::CallFirst(a_w, L"SetFont", copy.data(), copy.size());
	}
}

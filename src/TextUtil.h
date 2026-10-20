#pragma once

// Text helpers shared by both plugins: UTF-8 <-> UTF-16, code points, and the search's case / accent folding.
// Header only, no game dependencies (Windows.h only), so a throwaway test can include it alone.

#include <cstdint>
#include <string>
#include <string_view>
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace textutil
{
	// UTF-8 to UTF-16 (a name with umlauts, accents or Cyrillic must not be widened byte by byte). Malformed bytes become U+FFFD.
	inline std::wstring Wide(std::string_view a_s)
	{
		const int n = a_s.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), nullptr, 0);
		std::wstring out(n > 0 ? static_cast<std::size_t>(n) : 0, L'\0');
		if (n > 0) {
			MultiByteToWideChar(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), out.data(), n);
		}
		return out;
	}

	// UTF-16 to UTF-8
	inline std::string Utf8(std::wstring_view a_s)
	{
		const int n = a_s.empty() ? 0 : WideCharToMultiByte(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), nullptr, 0, nullptr, nullptr);
		std::string out(n > 0 ? static_cast<std::size_t>(n) : 0, '\0');
		if (n > 0) {
			WideCharToMultiByte(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), out.data(), n, nullptr, nullptr);
		}
		return out;
	}

	inline bool IsAscii(std::string_view a_s)
	{
		for (const char c : a_s) {
			if (static_cast<unsigned char>(c) >= 0x80) return false;
		}
		return true;
	}

	// A code point a search text may hold: printable, no control character (C0, DEL, C1), no line / paragraph separator, no surrogate half,
	// no noncharacter and not the replacement character (what malformed input turns into).
	inline bool Typable(char32_t a_cp)
	{
		if (a_cp < 0x20 || (a_cp >= 0x7F && a_cp < 0xA0)) return false;
		if (a_cp == 0x2028 || a_cp == 0x2029 || a_cp == 0xFFFD || (a_cp & 0xFFFE) == 0xFFFE) return false;
		return a_cp < 0xD800 || (a_cp > 0xDFFF && a_cp <= 0x10FFFF);
	}

	inline void AppendCodePoint(std::string& a_s, char32_t a_cp)
	{
		if (a_cp < 0x80) {
			a_s.push_back(static_cast<char>(a_cp));
		} else if (a_cp < 0x800) {
			a_s.push_back(static_cast<char>(0xC0 | (a_cp >> 6)));
			a_s.push_back(static_cast<char>(0x80 | (a_cp & 0x3F)));
		} else if (a_cp < 0x10000) {
			a_s.push_back(static_cast<char>(0xE0 | (a_cp >> 12)));
			a_s.push_back(static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F)));
			a_s.push_back(static_cast<char>(0x80 | (a_cp & 0x3F)));
		} else {
			a_s.push_back(static_cast<char>(0xF0 | (a_cp >> 18)));
			a_s.push_back(static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F)));
			a_s.push_back(static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F)));
			a_s.push_back(static_cast<char>(0x80 | (a_cp & 0x3F)));
		}
	}

	// the number of code points of a UTF-8 text (every byte that is not a continuation byte starts one)
	inline std::size_t CodePoints(std::string_view a_s)
	{
		std::size_t n = 0;
		for (const char c : a_s) {
			if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
		}
		return n;
	}

	// Backspace: the last code point goes, all of its bytes
	inline void PopCodePoint(std::string& a_s)
	{
		while (!a_s.empty() && (static_cast<unsigned char>(a_s.back()) & 0xC0) == 0x80) a_s.pop_back();
		if (!a_s.empty()) a_s.pop_back();
	}

	// Calls a_fn(code point) for each code point of a UTF-16 text; a lone surrogate half is passed on as is (Typable rejects it).
	template <class F>
	void ForEachCodePoint(std::wstring_view a_s, F&& a_fn)
	{
		for (std::size_t i = 0; i < a_s.size(); ++i) {
			const char32_t c = a_s[i];
			if (c >= 0xD800 && c <= 0xDBFF && i + 1 < a_s.size() && a_s[i + 1] >= 0xDC00 && a_s[i + 1] <= 0xDFFF) {
				a_fn(static_cast<char32_t>(0x10000 + ((c - 0xD800) << 10) + (a_s[i + 1] - 0xDC00)));
				++i;
			} else {
				a_fn(c);
			}
		}
	}

	// The search text as the filter keeps it: lower case (Unicode, not only A-Z), so that the "same text" tests of the filter ignore case.
	inline std::string Lower(std::string_view a_s)
	{
		if (IsAscii(a_s)) {
			std::string out(a_s);
			for (auto& c : out) {
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			}
			return out;
		}
		const std::wstring w = Wide(a_s);
		std::wstring       lower(w.size(), L'\0');
		const int          n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), static_cast<int>(w.size()), lower.data(), static_cast<int>(lower.size()), nullptr, nullptr, 0);
		if (n <= 0) return std::string(a_s);   // the map failed: the text as it is
		lower.resize(static_cast<std::size_t>(n));
		return Utf8(lower);
	}

	// Text a font made for Latin letters (Robinson, the game's list font outside Japan and China) cannot draw: any letter above Latin Extended-B
	// (Greek, Cyrillic, Hangul, kana, Han ...). General punctuation, currency signs and the like do not count.
	inline bool NeedsWideFont(std::string_view a_utf8)
	{
		bool need = false;
		ForEachCodePoint(Wide(a_utf8), [&](char32_t a_cp) {
			if (a_cp > 0x24F && !(a_cp >= 0x2000 && a_cp <= 0x2BFF) && !(a_cp >= 0x1E00 && a_cp <= 0x1EFF)) need = true;
		});
		return need;
	}

	// Does a_name hold a_query, ignoring case (and, with a_ignoreAccents, accents of a plain ASCII query: "cafe" finds "Café", but "café" does not find "cafe")? Both are UTF-8. Two plain ASCII texts take
	// the cheap byte compare; anything else goes through the system's linguistic search. An empty query is found everywhere.
	inline bool Contains(std::string_view a_name, std::string_view a_query, bool a_ignoreAccents)
	{
		if (a_query.empty()) return true;
		if (IsAscii(a_name) && IsAscii(a_query)) {
			if (a_query.size() > a_name.size()) return false;
			for (std::size_t i = 0; i + a_query.size() <= a_name.size(); ++i) {
				std::size_t j = 0;
				for (; j < a_query.size(); ++j) {
					char a = a_name[i + j], b = a_query[j];
					if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
					if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
					if (a != b) break;
				}
				if (j == a_query.size()) return true;
			}
			return false;
		}
		const std::wstring name = Wide(a_name), query = Wide(a_query);
		if (name.empty() || query.empty()) return false;
		// Japanese: hiragana finds katakana (a player types ぽーしょん for ポーション); half- and full-width forms are one (the IME's full-width Latin letters, half-width katakana).
		DWORD flags = FIND_FROMSTART | LINGUISTIC_IGNORECASE | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH;
		if (a_ignoreAccents && IsAscii(a_query)) flags |= LINGUISTIC_IGNOREDIACRITIC;   // plain letters also find accented ones; a typed accent (or any non-ASCII letter) means exactly that letter
		return FindNLSStringEx(LOCALE_NAME_INVARIANT, flags, name.c_str(), static_cast<int>(name.size()), query.c_str(), static_cast<int>(query.size()), nullptr, nullptr, nullptr, 0) >= 0;
	}
}

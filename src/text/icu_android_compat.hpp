// icu_android_compat.hpp
//
// ICU on Android. The app runs from Android 11 (API 30) on, but the NDK only
// publishes ICU4C for apps from Android 12 on (API 31, libicu.so) -- and
// even there only a SUBSET of it. On Android Alice uses only the text
// breaking (ubrk_*: characters, words and lines) and the text direction (ubidi_*).
//
//  1) ubrk_*: nothing is linked against libicu. On the first call, dlopen("libicu.so"):
//     - Android 12+: uses the system ICU (result identical to before);
//     - Android 11: there is no public ICU, so it uses the simple implementation
//       below (alice_icu::fallback_*), good for languages with spaces between
//       words (Portuguese, English, German, Russian...). Chinese/Japanese break
//       between ideographs, in an approximate way.
//     The game's ubrk_* calls are redirected by #define to the
//     alice_icu::* functions (this header comes after <unicode/ubrk.h>).
//
//  2) ubrk_openBinaryRules()/ubrk_getBinaryRules() do not exist in the NDK (Alice
//     uses that to pre-compile the line/word/char breaking rules once
//     and reuse them). Here we reuse the SAME vectors
//     compiled_ubrk_rules/compiled_char_ubrk_rules/compiled_word_ubrk_rules
//     (see fonts.hpp) except that, on Android, instead of compiled rule bytes,
//     they hold [1 UBreakIteratorType byte][UTF-8 locale bytes].
//     ubrk_openBinaryRules() below decodes that and opens the iterator.
//
//  3) <unicode/ubidi.h> does not exist in the NDK -- bidi (right-to-left
//     Arabic/Hebrew text) has no equivalent there at all.
//     TODO: give real RTL support on Android (e.g.: a light port of the
//     Unicode BiDi algorithm). For now the stub below always treats the text
//     as a single LTR run -- RTL locales get the wrong visual order.
#pragma once

#ifdef __ANDROID__

#include <cstring>
#include <cstdint>
#include <string>

#include <dlfcn.h>

// ---- bidi: always a single LTR run (RTL off for now) ----

using UBiDiLevel = uint8_t;
enum UBiDiDirection { UBIDI_LTR = 0, UBIDI_RTL = 1, UBIDI_MIXED = 2, UBIDI_NEUTRAL = 3 };

struct UBiDi {
	int32_t text_length = 0;
};

inline UBiDi* ubidi_open() {
	return new UBiDi();
}

inline UBiDi* ubidi_openSized(int32_t /*maxLength*/, int32_t /*maxRunCount*/, UErrorCode* pErrorCode) {
	*pErrorCode = U_ZERO_ERROR;
	return new UBiDi();
}

inline void ubidi_close(UBiDi* bidi) {
	delete bidi;
}

inline void ubidi_setPara(UBiDi* bidi, const UChar* /*text*/, int32_t length, UBiDiLevel /*paraLevel*/,
		UBiDiLevel* /*embeddingLevels*/, UErrorCode* pErrorCode) {
	// TODO (post-Phase 1): honor paraLevel/locale_get_native_rtl once
	// there is bidi support on Android -- always LTR for now.
	bidi->text_length = length;
	*pErrorCode = U_ZERO_ERROR;
}

inline int32_t ubidi_countRuns(UBiDi* /*bidi*/, UErrorCode* pErrorCode) {
	*pErrorCode = U_ZERO_ERROR;
	return 1; // a single run, always
}

inline UBiDiDirection ubidi_getVisualRun(UBiDi* bidi, int32_t /*runIndex*/, int32_t* logicalStart, int32_t* length) {
	*logicalStart = 0;
	*length = bidi->text_length;
	return UBIDI_LTR;
}

// ---- ubrk_*: system ICU (API 31+) or our own implementation (API 30) ----

namespace alice_icu {

using open_fn = UBreakIterator* (*)(UBreakIteratorType, const char*, const UChar*, int32_t, UErrorCode*);
using close_fn = void (*)(UBreakIterator*);
using move_fn = int32_t (*)(UBreakIterator*);

struct system_icu_api {
	open_fn open = nullptr;
	close_fn close = nullptr;
	move_fn first = nullptr;
	move_fn next = nullptr;
	move_fn previous = nullptr;
};

// nullptr when the device has no public libicu.so (Android 11)
inline system_icu_api const* system_icu() {
	static system_icu_api const* const api = []() -> system_icu_api const* {
		void* lib = dlopen("libicu.so", RTLD_NOW | RTLD_LOCAL);
		if(!lib)
			return nullptr;
		static system_icu_api loaded;
		loaded.open = reinterpret_cast<open_fn>(dlsym(lib, "ubrk_open"));
		loaded.close = reinterpret_cast<close_fn>(dlsym(lib, "ubrk_close"));
		loaded.first = reinterpret_cast<move_fn>(dlsym(lib, "ubrk_first"));
		loaded.next = reinterpret_cast<move_fn>(dlsym(lib, "ubrk_next"));
		loaded.previous = reinterpret_cast<move_fn>(dlsym(lib, "ubrk_previous"));
		if(!loaded.open || !loaded.close || !loaded.first || !loaded.next || !loaded.previous)
			return nullptr;
		return &loaded;
	}();
	return api;
}

// --- custom implementation (Android 11) ------------------------------------

inline bool is_high_surrogate(UChar c) { return c >= 0xD800 && c <= 0xDBFF; }
inline bool is_low_surrogate(UChar c) { return c >= 0xDC00 && c <= 0xDFFF; }

// code point that starts at i (or the one that ends right before i, with before=true)
inline uint32_t code_point_at(const UChar* text, int32_t length, int32_t i) {
	UChar const c = text[i];
	if(is_high_surrogate(c) && i + 1 < length && is_low_surrogate(text[i + 1]))
		return 0x10000 + ((uint32_t(c) - 0xD800) << 10) + (uint32_t(text[i + 1]) - 0xDC00);
	return c;
}
inline uint32_t code_point_before(const UChar* text, int32_t i) {
	UChar const c = text[i - 1];
	if(is_low_surrogate(c) && i >= 2 && is_high_surrogate(text[i - 2]))
		return 0x10000 + ((uint32_t(text[i - 2]) - 0xD800) << 10) + (uint32_t(c) - 0xDC00);
	return c;
}

// marks that attach to the previous character (combining accents, variation selectors...)
inline bool is_extend(uint32_t cp) {
	return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) || (cp >= 0x0591 && cp <= 0x05BD)
		|| (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x0900 && cp <= 0x0903)
		|| (cp >= 0x093A && cp <= 0x094F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF)
		|| cp == 0x200C || cp == 0x200D || (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0x302A && cp <= 0x302F)
		|| (cp >= 0x3099 && cp <= 0x309A) || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F)
		|| (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

inline bool is_space(uint32_t cp) {
	return cp == ' ' || cp == '\t' || cp == 0xA0 || cp == 0x3000 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F;
}
inline bool is_newline(uint32_t cp) {
	return cp == '\n' || cp == '\r' || cp == 0x85 || cp == 0x2028 || cp == 0x2029;
}

// ideographs/syllabaries that do not use spaces: each one is a "word"
inline bool is_ideographic(uint32_t cp) {
	return (cp >= 0x2E80 && cp <= 0x2FFF) || (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3100 && cp <= 0x31FF)
		|| (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF)
		|| (cp >= 0x20000 && cp <= 0x3FFFF);
}

// Korean syllables: words separated by spaces, but the line may break between syllables
inline bool is_hangul(uint32_t cp) {
	return (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F);
}

// ASCII/Latin-1 punctuation and the general punctuation/symbol ranges
inline bool is_punctuation(uint32_t cp) {
	if(cp < 0x80)
		return !((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) && cp > ' ';
	return (cp >= 0xA1 && cp <= 0xBF && cp != 0xAA && cp != 0xB2 && cp != 0xB3 && cp != 0xB5 && cp != 0xB9 && cp != 0xBA)
		|| cp == 0xD7 || cp == 0xF7 || (cp >= 0x2010 && cp <= 0x2027) || (cp >= 0x2030 && cp <= 0x205E)
		|| (cp >= 0x20A0 && cp <= 0x20CF) || (cp >= 0x2190 && cp <= 0x2BFF) || (cp >= 0x3001 && cp <= 0x3003)
		|| (cp >= 0x3008 && cp <= 0x3011) || (cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20);
}

inline bool is_word_char(uint32_t cp) {
	return !is_space(cp) && !is_newline(cp) && !is_punctuation(cp) && !is_ideographic(cp) && !is_extend(cp);
}

inline bool is_digit(uint32_t cp) {
	return (cp >= '0' && cp <= '9') || (cp >= 0x0660 && cp <= 0x0669) || (cp >= 0xFF10 && cp <= 0xFF19);
}

// punctuation that does not split the word when it sits between two letters ("don't",
// "e.g") or between two numbers ("1,234.56") -- like ICU's MidLetter/MidNum
inline bool is_mid_letter(uint32_t cp) {
	return cp == '\'' || cp == 0x2019 || cp == '.' || cp == ':' || cp == 0xB7 || cp == 0x2027;
}
inline bool is_mid_number(uint32_t cp) {
	return cp == '\'' || cp == 0x2019 || cp == '.' || cp == ',' || cp == ';' || cp == 0x066C;
}

// "visible character" boundary (simplified grapheme cluster)
inline bool is_character_boundary(const UChar* text, int32_t length, int32_t i) {
	if(i <= 0 || i >= length)
		return true;
	if(is_low_surrogate(text[i]) && is_high_surrogate(text[i - 1]))
		return false; // middle of a surrogate pair
	if(text[i - 1] == '\r' && text[i] == '\n')
		return false;
	uint32_t const cp = code_point_at(text, length, i);
	if(is_extend(cp))
		return false;
	return code_point_before(text, i) != 0x200D; // emoji with ZWJ
}

// 0: word, 1: space, 2: line break, 3: other (each one alone)
inline int word_class(uint32_t cp) {
	if(is_word_char(cp))
		return 0;
	if(is_space(cp))
		return 1;
	if(is_newline(cp))
		return 2;
	return 3;
}

inline bool is_word_boundary(const UChar* text, int32_t length, int32_t i) {
	if(i <= 0 || i >= length)
		return true;
	if(!is_character_boundary(text, length, i))
		return false;
	uint32_t const before = code_point_before(text, i);
	uint32_t const after = code_point_at(text, length, i);
	int const cb = word_class(before);
	int const ca = word_class(after);
	if(cb == 0 && ca == 0)
		return false;
	if(cb == 1 && ca == 1)
		return false; // consecutive spaces stay together
	if(before == '\r' && after == '\n')
		return false;
	// letter + apostrophe/period + letter (or number + comma/period + number) stays together
	auto joins = [](uint32_t left, uint32_t mid, uint32_t right) {
		bool const letters = word_class(left) == 0 && word_class(right) == 0 && !is_digit(left) && !is_digit(right);
		bool const numbers = is_digit(left) && is_digit(right);
		return (letters && is_mid_letter(mid)) || (numbers && is_mid_number(mid));
	};
	if(cb == 0) { // before: word; after: maybe mid punctuation
		int32_t const j = i + (after > 0xFFFF ? 2 : 1);
		if(j < length && joins(before, after, code_point_at(text, length, j)))
			return false;
	}
	if(ca == 0) { // after: word; before: maybe mid punctuation
		int32_t const j = i - (before > 0xFFFF ? 2 : 1);
		if(j > 0 && joins(code_point_before(text, j), before, after))
			return false;
	}
	return true;
}

// line break opportunity before i
inline bool is_line_boundary(const UChar* text, int32_t length, int32_t i) {
	if(i <= 0 || i >= length)
		return true;
	if(!is_character_boundary(text, length, i))
		return false;
	uint32_t const before = code_point_before(text, i);
	uint32_t const after = code_point_at(text, length, i);
	if(is_newline(before))
		return !(before == '\r' && after == '\n'); // mandatory break
	if(is_space(after) || is_newline(after))
		return false; // never before a space (spaces stay at the end of the line)
	if(before == 0xA0 || after == 0xA0 || before == 0x202F || after == 0x202F)
		return false; // non-breaking space
	// never before closing/infix punctuation, nor after spaces
	// (classes CL, CP, EX, IS and SY of the Unicode line breaking algorithm)
	bool const no_break_before = after == ')' || after == ']' || after == '}' || after == '!' || after == '?'
		|| after == ',' || after == '.' || after == ':' || after == ';' || after == '/' || after == 0x00BB
		|| after == 0x3001 || after == 0x3002 || after == 0xFF0C || after == 0xFF0E || after == 0x300D || after == 0x300F;
	if(no_break_before)
		return false;
	if(is_space(before))
		return true; // after one or more spaces
	if(before == '/')
		return !is_digit(after); // "input/|output", but "1/2" stays together
	if(before == '-' || before == 0x2010 || before == 0x2013 || before == 0x2014)
		return is_word_char(after) && i >= 2 && is_word_char(code_point_before(text, i - (before > 0xFFFF ? 2 : 1)));
	if(is_ideographic(before) || is_ideographic(after) || is_hangul(before) || is_hangul(after)) {
		// do not break before closing punctuation nor after opening punctuation
		bool const closing = after == 0x3001 || after == 0x3002 || after == 0xFF0C || after == 0xFF0E || after == 0x300D || after == 0x300F || after == ')' || after == ',' || after == '.' || after == '!' || after == '?';
		bool const opening = before == 0x300C || before == 0x300E || before == '(';
		return !closing && !opening;
	}
	return false;
}

struct break_iterator {
	UBreakIterator* system = nullptr; // system ICU, when it exists
	UBreakIteratorType type = UBRK_CHARACTER;
	const UChar* text = nullptr;
	int32_t length = 0;
	int32_t position = 0;

	bool is_boundary(int32_t i) const {
		switch(type) {
		case UBRK_WORD: return is_word_boundary(text, length, i);
		case UBRK_LINE: return is_line_boundary(text, length, i);
		default: return is_character_boundary(text, length, i);
		}
	}
};

inline break_iterator* as_iterator(UBreakIterator* bi) {
	return reinterpret_cast<break_iterator*>(bi);
}

inline UBreakIterator* open(UBreakIteratorType type, const char* locale, const UChar* text, int32_t text_length, UErrorCode* status) {
	auto* it = new break_iterator();
	it->type = type;
	if(auto const* api = system_icu(); api) {
		it->system = api->open(type, locale, text, text_length, status);
		if(!it->system) {
			delete it;
			return nullptr;
		}
	} else {
		it->text = text;
		it->length = text ? (text_length >= 0 ? text_length : int32_t(std::char_traits<char16_t>::length(reinterpret_cast<const char16_t*>(text)))) : 0;
		*status = U_ZERO_ERROR;
	}
	return reinterpret_cast<UBreakIterator*>(it);
}

inline void close(UBreakIterator* bi) {
	auto* it = as_iterator(bi);
	if(!it)
		return;
	if(it->system)
		system_icu()->close(it->system);
	delete it;
}

inline int32_t first(UBreakIterator* bi) {
	auto* it = as_iterator(bi);
	if(it->system)
		return system_icu()->first(it->system);
	it->position = 0;
	return 0;
}

inline int32_t next(UBreakIterator* bi) {
	auto* it = as_iterator(bi);
	if(it->system)
		return system_icu()->next(it->system);
	if(it->position >= it->length)
		return UBRK_DONE;
	int32_t i = it->position + 1;
	while(i < it->length && !it->is_boundary(i))
		++i;
	it->position = i;
	return i;
}

inline int32_t previous(UBreakIterator* bi) {
	auto* it = as_iterator(bi);
	if(it->system)
		return system_icu()->previous(it->system);
	if(it->position <= 0)
		return UBRK_DONE;
	int32_t i = it->position - 1;
	while(i > 0 && !it->is_boundary(i))
		--i;
	it->position = i;
	return i;
}

} // namespace alice_icu

// redirects the game's calls (the NDK declarations only exist on API 31+)
#define ubrk_open alice_icu::open
#define ubrk_close alice_icu::close
#define ubrk_first alice_icu::first
#define ubrk_next alice_icu::next
#define ubrk_previous alice_icu::previous

// ---- ubrk_openBinaryRules: decodes [type][locale] and opens the iterator ----

inline UBreakIterator* ubrk_openBinaryRules(const uint8_t* binaryRules, int32_t rulesLength,
		const UChar* text, int32_t textLength, UErrorCode* status) {
	if(rulesLength < 1) {
		*status = U_ILLEGAL_ARGUMENT_ERROR;
		return nullptr;
	}
	auto type = UBreakIteratorType(binaryRules[0]);
	// following bytes = locale in UTF-8/ASCII (always is, see fonts.cpp) -- needs
	// a zero-terminated char*, so it copies to a local buffer
	char locale_buf[64] = {};
	int32_t locale_len = rulesLength - 1;
	if(locale_len >= int32_t(sizeof(locale_buf)))
		locale_len = int32_t(sizeof(locale_buf)) - 1;
	std::memcpy(locale_buf, binaryRules + 1, size_t(locale_len));
	locale_buf[locale_len] = '\0';
	return ubrk_open(type, locale_buf, text, textLength, status);
}

#endif // __ANDROID__

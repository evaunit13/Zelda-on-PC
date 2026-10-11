#pragma once

// Zelda on PC - keyboard & mouse button prompts.
//
// The game draws its button prompts with glyphs U+E040..U+E08F of Font/Font_EU.sbfarc:External_00.bffnt.
// At startup we take that archive from the player's own dump, swap External_00.bffnt for the PC Edition prompt
// font (resources/pcedition/External_00.bffnt, our own artwork: one glyph per key / mouse button at U+E100+VK,
// composites at U+E0F0..), point the game's button codes at the glyphs of the current key bindings and write the
// archive (plus the matching ResourceSizeTable entry) into the PC Edition graphic pack's content folder.
// At runtime, prompts shown by the game are re-targeted per context (menus: click / Esc, scope and runes: mouse
// buttons, ...) by rewriting the glyph codes of the text boxes (see PCUI).

#include "pcedition/PCKeys.h"
#include "pcedition/PCSettings.h"

namespace pce::Prompts
{
	// generate / refresh the font archive in the pack's content folder (fast when nothing changed)
	void Prepare(const fs::path& packDir);

	// glyph code of a key (U+E100+VK), or 0 if the prompt font has no glyph for it
	uint16 KeyGlyph(Key k);
	// glyph code for the first bound key of an action (0 if unbound)
	uint16 ActionGlyph(const Settings& s, Action a);

	// composite glyphs
	constexpr uint16 kGlyphWASD = 0xE0F0;
	constexpr uint16 kGlyphAD = 0xE0F1;
	constexpr uint16 kGlyphWS = 0xE0F2;
	constexpr uint16 kGlyphMouseMove = 0xE0F3;
	constexpr uint16 kGlyphMouseX = 0xE0F4;
	constexpr uint16 kGlyphMouseY = 0xE0F5;
	constexpr uint16 kGlyphArrowKeys = 0xE0F6;
	constexpr uint16 kGlyphWheelUpDown = 0xE0F7;
	constexpr uint16 kGlyphOneToFour = 0xE0F8;
	constexpr uint16 kGlyphMouseLeft = 0xE0F9;
	constexpr uint16 kGlyphMouseRight = 0xE0FA;
	constexpr uint16 kGlyphMouseUp = 0xE0FB;
	constexpr uint16 kGlyphMouseDown = 0xE0FC;

	// the PC glyph for one of the game's button codes (U+E040..U+E067) according to the current bindings
	// (gameplay meaning); returns the code unchanged if it is not a button code
	uint16 DefaultGlyphFor(const Settings& s, uint16 gameCode);

	// true once the prompt font is installed for this session (substitution only makes sense then)
	bool Active();

	// The PC layer recognises situations by the texts of the game's own action guides and buttons ("Dash", "Let go",
	// "Cancel", "Save", ...), in English. With another text language (Settings::language) those texts are translated
	// back: the dump has one message pack per language, and a message has the same file and label in all of them.
	void LoadGuideLanguage();                                // once at startup, after the game was located
	const std::string& GuideToEnglish(const std::string& text); // the English text of a guide, or the text itself
}

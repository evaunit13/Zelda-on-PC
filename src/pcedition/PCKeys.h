#pragma once

// Zelda on PC - key identifiers.
// Keyboard keys use Windows virtual-key codes (left/right modifiers are distinguished).
// Mouse buttons reuse VK_LBUTTON etc., the wheel uses two otherwise unassigned VK slots.

#include <string>
#include <string_view>

namespace pce
{
	enum class Key : uint16
	{
		None = 0,
		MouseLeft = 0x01,   // VK_LBUTTON
		MouseRight = 0x02,  // VK_RBUTTON
		MouseMiddle = 0x04, // VK_MBUTTON
		Mouse4 = 0x05,      // VK_XBUTTON1
		Mouse5 = 0x06,      // VK_XBUTTON2
		WheelUp = 0x0E,     // unassigned VK
		WheelDown = 0x0F,   // unassigned VK
		Backspace = 0x08,
		Tab = 0x09,
		Enter = 0x0D,
		Escape = 0x1B,
		Space = 0x20,
		PageUp = 0x21,
		PageDown = 0x22,
		End = 0x23,
		Home = 0x24,
		Left = 0x25,
		Up = 0x26,
		Right = 0x27,
		Down = 0x28,
		Insert = 0x2D,
		Delete = 0x2E,
		// '0'..'9' = 0x30..0x39, 'A'..'Z' = 0x41..0x5A
		F1 = 0x70,
		F12 = 0x7B,
		LShift = 0xA0,
		RShift = 0xA1,
		LCtrl = 0xA2,
		RCtrl = 0xA3,
		LAlt = 0xA4,
		RAlt = 0xA5,
		Max = 0x100
	};

	constexpr Key KeyChar(char c) { return (Key)(uint16)c; }

	inline bool IsMouseKey(Key k)
	{
		return k == Key::MouseLeft || k == Key::MouseRight || k == Key::MouseMiddle || k == Key::Mouse4 || k == Key::Mouse5 || k == Key::WheelUp || k == Key::WheelDown;
	}

	// stable identifier used in settings.ini ("W", "Space", "LShift", "MouseLeft", "WheelUp", ...)
	std::string KeyToId(Key k);
	Key KeyFromId(std::string_view id);
	// human readable, localized to the keyboard layout where possible ("E", "Space", "Left Shift", ...)
	std::string KeyDisplayName(Key k);
	// short label for key-cap prompts ("E", "SPACE", "SHIFT", "TAB", "F1", ...); empty for mouse keys
	std::string KeyCapLabel(Key k);
}

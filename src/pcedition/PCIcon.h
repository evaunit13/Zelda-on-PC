#pragma once

// Zelda on PC - the game's own icon for the window and the shortcuts.
//
// The project ships none of the game's artwork. The icon is made on the player's machine from the icon of their own
// game dump (meta/iconTex.tga of the base game, 128 x 128) and stays there: the installer has it written at install
// time for the Start menu / desktop shortcuts, and the game writes it at startup if it is missing and puts it on its
// window (title bar, task bar, Alt+Tab). Without it everything falls back to the icon built into the executable.

namespace pce::Icon
{
	// Writes a multi-size .ico (rounded corners, thin outline) from <baseTitle>/meta/iconTex.tga. False if the dump has
	// no usable icon or the file cannot be written.
	bool WriteFromDump(const fs::path& baseTitle, const fs::path& icoPath);
	// Puts the icon file on a top level window. False if the file cannot be loaded.
	bool ApplyToWindow(void* hwnd, const fs::path& icoPath);
}

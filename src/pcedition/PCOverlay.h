#pragma once

// Zelda on PC - in-game settings menu (drawn with ImGui over the game image).

namespace pce::Overlay
{
	void SetOpen(bool open);
	void DebugSetPage(int page);
	bool IsOpen();
	void Toggle();

	// called every frame before ImGui::NewFrame (font atlas changes are only allowed there)
	void PrepareFrame();
	// called every frame inside the ImGui frame of the main window
	void Render();
	// true while the overlay wants the game to be paused
	bool WantsPause();
	// called from VPADRead on the game's main thread: blocks while the menu is open (pauses the game loop)
	void BlockWhileOpen();
	bool IsGamePaused();
	// unblocks a paused game loop (application is closing)
	void ReleaseForShutdown();
	// loading screen indicator (inside an ImGui frame), progress 0..1
	void DrawLoadingIndicator(float progress);
}

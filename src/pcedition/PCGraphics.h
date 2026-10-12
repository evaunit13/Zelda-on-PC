#pragma once

// Zelda on PC - graphics options backed by Cemu graphic packs.

#include <string>
#include <vector>

namespace pce::Graphics
{
	struct ResolutionOption
	{
		int width, height;
		std::string label;
	};
	// Aspect ratio the game renders in: 1 16:9, 2 16:10, 3 21:9, 4 32:9. For Settings::aspectRatio = 0 it follows the
	// main display, for the ultrawide formats only (a 16:10 display keeps 16:9 unless 16:10 is chosen).
	int EffectiveAspect(int setting);
	const std::vector<std::string>& GetAspectOptions();
	// the resolutions of an aspect ratio (same number of entries and the same heights for all of them)
	const std::vector<ResolutionOption>& GetResolutionOptions(int aspect = 1);
	// The picture inside the game window (it has bars when the window has another shape), in pixels of the window's
	// client area. false while there is no picture yet.
	bool GetPictureArea(int& x, int& y, int& width, int& height, int& windowWidth, int& windowHeight);
	const std::vector<std::string>& GetShadowOptions();
	const std::vector<std::string>& GetAAOptions();
	const std::vector<int>& GetFpsOptions();

	// copies community packs (read-only source: the user's Cemu install) and writes the PC Edition pack
	void PrepareGraphicPacks();
	// after GraphicPack2::LoadAll(): enable packs and select presets from settings.ini
	void ApplySettingsToPacks();
	// at runtime from the settings menu: re-apply live-capable options (FPS limit, AA, vsync)
	// returns true if some change requires a restart (resolution/shadows)
	bool ApplyLive();
	bool RestartPending();
	// directory of the PC Edition graphic pack (patches + content replacements)
	fs::path GetEditionPackDir();
}

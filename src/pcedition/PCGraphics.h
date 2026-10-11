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
	const std::vector<ResolutionOption>& GetResolutionOptions();
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

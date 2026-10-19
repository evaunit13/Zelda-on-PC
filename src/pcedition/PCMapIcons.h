#pragma once

// BotW PC Edition - Interactive World Map overlay (inspired by zeldamods/objmap).
// Renders custom vector icons, item/chest/equipment search, category filters, and live save-flag auto-hiding
// directly onto the Sheikah Slate world map.

#include <string>
#include <vector>

namespace pce::MapIcons
{
	enum Category : uint32
	{
		Cat_Shrine     = 1u << 0,  // 120 Base + 16 DLC Shrines + Final Trial
		Cat_Tower      = 1u << 1,  // 15 Sheikah Towers
		Cat_Korok      = 1u << 2,  // 900 Korok Seeds
		Cat_Chest      = 1u << 3,  // ~1315 Overworld Treasure Chests
		Cat_Boss       = 1u << 4,  // Overworld Bosses (Lynel, Hinox/Stalnox, Talus, Molduga)
		Cat_Guardian   = 1u << 5,  // Overworld Guardians (Stalker, Skywatcher, Turret, Decayed)
		Cat_Settlement = 1u << 6,  // Villages, Stables, Tech Labs, Great Fairies, Statues, Cooking Pots
		Cat_Weapon     = 1u << 7,  // Standalone Overworld Weapons, Bows, Shields
		Cat_Ore        = 1u << 8,  // Ore, Rare Ore, Luminous Stone Deposits & Gems
		Cat_Material   = 1u << 9,  // Plants, Mushrooms, Fruits, Critters, Fish
		Cat_Enemy      = 1u << 10, // Enemies carrying weapons/equipment
		Cat_Count      = 11,
		Cat_Default    = Cat_Shrine | Cat_Tower | Cat_Korok | Cat_Chest | Cat_Boss,
		Cat_All        = (1u << Cat_Count) - 1u
	};

	// Renders interactive map markers, filter/search panel, and tooltips when the Sheikah Slate map is open.
	// Returns true while the map screen is active.
	bool Render(double now, float dt);

	// Developer bridge diagnostics & automation command ("mapicons ...")
	std::string DebugCommand(const std::vector<std::string>& args);
}

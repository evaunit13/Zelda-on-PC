#pragma once

// Zelda on PC - top level integration with Cemu.

#include <set>
#include <string>
#include <vector>

namespace pce
{
	constexpr const char* kGameTitle = "The Legend of Zelda: Breath of the Wild";
	// the game code this edition is written for: update 1.5.0 (title version 208, the same executable in all regions)
	constexpr uint32 kRequiredUpdateVersion = 208;

	// command line: --cemu-ui (plain Cemu UI for debugging), --pce-dev (developer bridge + verbose log)
	void ParseCommandLine(const std::vector<std::wstring>& args);
	// Command line tools that do their job and end the process without starting the game. Returns the exit code, or
	// nothing if the command line asks for none of them.
	//   --extract-icon <game folder> <out.ico>   used by the installer: icon for the shortcuts (see PCIcon.h)
	std::optional<int> RunCommandLineTool(const std::vector<std::wstring>& args);
	bool IsActive();   // PC Edition mode (kiosk, native input, ...)
	bool IsDevMode();
	bool ForceWindowed(); // --windowed (testing): start windowed without changing settings.ini

	// paths: <exe>/data holds all writable state (Cemu config, mlc, caches, graphic packs) + <exe>/settings.ini
	fs::path GetExeDir();
	fs::path GetDataDir();
	void DeterminePaths(const fs::path& exePath, std::set<fs::path>& failedWriteAccess);

	// called once after Cemu's config has been loaded (before CemuCommonInit)
	void InitBeforeCemu();
	// re-applies the forced Cemu configuration (Cemu reloads settings.xml during init)
	void ReapplyForcedConfig();
	// called after the main window exists
	void InitAfterWindow(void* mainHwnd);
	void Shutdown();

	// game location: the player's own dump as loose title folders (code / content / meta). The base game, the update and
	// the DLC may sit anywhere below the configured folder, next to the base game folder, or in the title folder of an
	// existing Cemu installation.
	fs::path GetGameRoot();        // configured folder
	fs::path GetBaseTitlePath();   // base game title folder
	fs::path GetUpdateTitlePath(); // update title folder (empty if not found)
	fs::path GetAocTitlePath();    // DLC title folder (empty if not found)
	bool IsGameAvailable();
	uint32 GetUpdateVersion();     // title version of the update that was found (0 if none)

	// fatal startup error shown in a minimal native message box (only used if the game cannot start at all)
	void FatalError(const std::string& message);
}

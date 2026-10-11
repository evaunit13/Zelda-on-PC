#include "pcedition/PCEdition.h"
#include "pcedition/PCSettings.h"
#include "pcedition/PCInput.h"
#include "pcedition/PCGraphics.h"
#include "pcedition/PCDevBridge.h"
#include "pcedition/PCUI.h"
#include "pcedition/PCIcon.h"

#include <windows.h>
#include <shlobj.h>
#include <boost/algorithm/string.hpp>
#include <pugixml.hpp>

#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"

namespace pce
{
	namespace
	{
		bool sActive = true;
		bool sDevMode = false;
		bool sForceWindowed = false;
		fs::path sExeDir;
		fs::path sGameRoot;
		fs::path sBasePath, sUpdatePath, sAocPath;
		uint32 sRegion = 0;        // low half of the title id (0x101C9500 Europe, 0x101C9400 Americas, 0x101C9300 Japan)
		uint32 sUpdateVersion = 0;

		constexpr uint32 kHighBase = 0x00050000, kHighUpdate = 0x0005000E, kHighAoc = 0x0005000C;

		bool IsGameRegion(uint32 low)
		{
			return low == 0x101C9500 || low == 0x101C9400 || low == 0x101C9300;
		}

		fs::path RoamingAppData()
		{
			PWSTR path = nullptr;
			fs::path r;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
				r = path;
			CoTaskMemFree(path);
			return r;
		}

		struct LooseTitle
		{
			uint64 titleId = 0;
			uint32 version = 0;
			fs::path path;
		};

		// read <code/app.xml> (title id, title version) of a loose title folder
		std::optional<LooseTitle> ReadLooseTitle(const fs::path& titleDir)
		{
			std::error_code ec;
			fs::path appXml = titleDir / "code" / "app.xml";
			if (!fs::exists(appXml, ec))
				return std::nullopt;
			pugi::xml_document doc;
			if (!doc.load_file(appXml.c_str()))
				return std::nullopt;
			auto node = doc.child("app").child("title_id");
			if (!node)
				return std::nullopt;
			LooseTitle t;
			try
			{
				t.titleId = std::stoull(node.text().as_string(), nullptr, 16);
				t.version = (uint32)std::stoul(doc.child("app").child("title_version").text().as_string("0"), nullptr, 16);
			}
			catch (const std::exception&)
			{
				return std::nullopt;
			}
			t.path = titleDir;
			return t;
		}

		// collect the game's title folders below a folder (limited depth; title folders are not searched further)
		void CollectTitles(const fs::path& dir, int depth, std::vector<LooseTitle>& out)
		{
			std::error_code ec;
			if (!fs::is_directory(dir, ec))
				return;
			if (auto t = ReadLooseTitle(dir))
			{
				if (IsGameRegion((uint32)t->titleId))
					out.push_back(*t);
				return;
			}
			if (depth <= 0)
				return;
			for (auto& e : fs::directory_iterator(dir, ec))
			{
				if (e.is_directory(ec))
					CollectTitles(e.path(), depth - 1, out);
			}
		}

		// title folder of an existing Cemu installation (titles installed with its "Install update / DLC")
		fs::path CemuTitleDir()
		{
			fs::path mlc = RoamingAppData() / "Cemu" / "mlc01";
			pugi::xml_document doc;
			if (doc.load_file((RoamingAppData() / "Cemu" / "settings.xml").c_str()))
			{
				const std::string custom = doc.child("content").child("mlc_path").text().as_string();
				if (!custom.empty())
					mlc = _utf8ToPath(custom);
			}
			return mlc / "usr" / "title";
		}

		// locate base game, update and DLC starting from a folder. False if the base game is not there.
		bool LocateGame(const fs::path& root)
		{
			std::vector<LooseTitle> titles;
			CollectTitles(root, 3, titles);
			// the base game folder itself was given: update and DLC are usually right next to it
			if (ReadLooseTitle(root))
				CollectTitles(root.parent_path(), 1, titles);
			const LooseTitle* base = nullptr;
			for (auto& t : titles)
			{
				if ((uint32)(t.titleId >> 32) == kHighBase)
				{
					base = &t;
					break;
				}
			}
			if (!base)
				return false;
			const uint32 region = (uint32)base->titleId;
			// the folders of an existing Cemu installation are searched last
			const fs::path cemuTitles = CemuTitleDir();
			for (uint32 high : {kHighUpdate, kHighAoc})
			{
				if (auto t = ReadLooseTitle(cemuTitles / fmt::format("{:08x}", high) / fmt::format("{:08x}", region)))
					titles.push_back(*t);
			}
			auto find = [&](uint32 high) -> const LooseTitle* {
				for (auto& t : titles)
				{
					if ((uint32)(t.titleId >> 32) == high && (uint32)t.titleId == region)
						return &t;
				}
				return nullptr;
			};
			sBasePath = base->path;
			sRegion = region;
			sUpdatePath.clear();
			sAocPath.clear();
			sUpdateVersion = 0;
			if (auto u = find(kHighUpdate))
			{
				sUpdatePath = u->path;
				sUpdateVersion = u->version;
			}
			if (auto a = find(kHighAoc))
				sAocPath = a->path;
			return true;
		}

		std::vector<fs::path> CandidateGameRoots()
		{
			std::vector<fs::path> r;
			// 1. explicitly configured
			Settings s = GetSettings();
			if (!s.gamePath.empty())
				r.emplace_back(_utf8ToPath(s.gamePath));
			// 2. next to the executable
			r.emplace_back(sExeDir / "game");
			// 3. game paths of an existing Cemu installation (read only)
			fs::path cemuSettings = RoamingAppData() / "Cemu" / "settings.xml";
			pugi::xml_document doc;
			if (doc.load_file(cemuSettings.c_str()))
			{
				for (auto e : doc.child("content").child("GamePaths").children("Entry"))
					r.emplace_back(_utf8ToPath(e.text().as_string()));
				for (auto e : doc.child("content").child("RecentLaunchFiles").children("Entry"))
					r.emplace_back(_utf8ToPath(e.text().as_string()).parent_path());
			}
			return r;
		}

		void CopyTreeIfMissing(const fs::path& src, const fs::path& dst)
		{
			std::error_code ec;
			if (!fs::exists(src, ec) || fs::exists(dst, ec))
				return;
			fs::create_directories(dst.parent_path(), ec);
			fs::copy(src, dst, fs::copy_options::recursive, ec);
			if (ec)
				cemuLog_log(LogType::Force, "PCE: failed to copy {} -> {}: {}", _pathToUtf8(src), _pathToUtf8(dst), ec.message());
			else
				cemuLog_log(LogType::Force, "PCE: imported {}", _pathToUtf8(src));
		}

		// one-time import of the user's existing Cemu save (copy, never move)
		void ImportExistingSave()
		{
			const fs::path cemuMlc = sDevMode ? sExeDir / "data" / "mlc01" : RoamingAppData() / "Cemu" / "mlc01";
			const fs::path mlc = ActiveSettings::GetMlcPath();
			const std::string save = fmt::format("usr/save/00050000/{:08x}", sRegion ? sRegion : 0x101C9500u);
			CopyTreeIfMissing(cemuMlc / save, mlc / save);
			CopyTreeIfMissing(cemuMlc / "usr/save/system/act", mlc / "usr/save/system/act");
		}

		void ForceCemuConfig()
		{
			auto& c = GetConfig();
			c.graphic_api = GraphicAPI::kVulkan;
			c.async_compile = true;
			c.vk_accurate_barriers = true;
			c.play_boot_sound = false;
			c.disable_screensaver = true;
			c.console_language = CafeConsoleLanguage::EN;
			c.overlay.position = ScreenPosition::kDisabled;
			c.notification.position = ScreenPosition::kDisabled;
			c.notification.controller_profiles = false;
			c.notification.shader_compiling = false;
			c.notification.friends = false;
			c.notification.controller_battery = false;
			c.upscale_filter = kBicubicFilter;
			c.downscale_filter = kLinearFilter;
			c.fullscreen_scaling = kKeepAspectRatio;
			c.crash_dump = CrashDump::Disabled;
			c.account.legacy_online_enabled = false;
			// Cemu's title list gets exactly the game's own title folders (nothing else is scanned)
			c.game_paths.clear();
			for (const fs::path* p : {&sBasePath, &sUpdatePath, &sAocPath})
			{
				if (!p->empty())
					c.game_paths.emplace_back(_pathToUtf8(*p));
			}
			Settings s = GetSettings();
			c.vsync = s.vsync ? 1 : 0;
			GetConfigHandle().Save();
		}
	}

	void ParseCommandLine(const std::vector<std::wstring>& args)
	{
		for (auto& a : args)
		{
			if (boost::iequals(a, L"--cemu-ui"))
				sActive = false;
			else if (boost::iequals(a, L"--pce-dev"))
				sDevMode = true;
			else if (boost::iequals(a, L"--windowed"))
				sForceWindowed = true;
		}
	}

	std::optional<int> RunCommandLineTool(const std::vector<std::wstring>& args)
	{
		for (size_t i = 0; i < args.size(); i++)
		{
			if (!boost::iequals(args[i], L"--extract-icon"))
				continue;
			if (i + 2 >= args.size())
				return 2;
			return LocateGame(fs::path(args[i + 1])) && Icon::WriteFromDump(sBasePath, fs::path(args[i + 2])) ? 0 : 1;
		}
		return std::nullopt;
	}

	bool IsActive()
	{
		return sActive;
	}

	bool IsDevMode()
	{
		return sDevMode;
	}

	bool ForceWindowed()
	{
		return sForceWindowed;
	}

	fs::path GetExeDir()
	{
		return sExeDir;
	}

	fs::path GetDataDir()
	{
		// developer instances never share state (saves, settings, caches) with the player's own sessions
		return sExeDir / (sDevMode ? "data_dev" : "data");
	}

	void DeterminePaths(const fs::path& exePath, std::set<fs::path>& failedWriteAccess)
	{
		sExeDir = exePath.parent_path();
		fs::path data = GetDataDir();
		std::error_code ec;
		fs::create_directories(data, ec);
		ActiveSettings::SetPaths(true, exePath, data, data, data, sExeDir, failedWriteAccess);
	}

	void InitBeforeCemu()
	{
		LoadSettings(sDevMode ? GetDataDir() / "settings.ini" : sExeDir / "settings.ini");
		// locate game
		for (auto& root : CandidateGameRoots())
		{
			if (!root.empty() && LocateGame(root))
			{
				sGameRoot = root;
				break;
			}
		}
		if (!sGameRoot.empty())
		{
			cemuLog_log(LogType::Force, "PCE: game found at {} (update: {}, version {}; DLC: {})", _pathToUtf8(sBasePath),
						sUpdatePath.empty() ? "not found" : _pathToUtf8(sUpdatePath), sUpdateVersion, sAocPath.empty() ? "not found" : _pathToUtf8(sAocPath));
			UpdateSettings([&](Settings& s) { if (s.gamePath.empty()) s.gamePath = _pathToUtf8(sGameRoot); });
			// window / shortcut icon: made here from the dump's own icon, never shipped (the installer usually wrote it)
			std::error_code ec;
			if (!fs::exists(GetDataDir() / "game.ico", ec))
				Icon::WriteFromDump(sBasePath, GetDataDir() / "game.ico");
		}
		else
			cemuLog_log(LogType::Force, "PCE: game not found");
		ForceCemuConfig();
		ImportExistingSave();
		Input::RegisterHLE();
		UI::RegisterHLE();
		Graphics::PrepareGraphicPacks();
	}

	void ReapplyForcedConfig()
	{
		ForceCemuConfig();
	}

	void InitAfterWindow(void* mainHwnd)
	{
		Input::SetMainWindow(mainHwnd);
		Icon::ApplyToWindow(mainHwnd, GetDataDir() / "game.ico");
		Input::Start();
		if (sDevMode)
			DevBridge::Start();
	}

	void Shutdown()
	{
		DevBridge::Stop();
		Input::Shutdown();
	}

	fs::path GetGameRoot()
	{
		return sGameRoot;
	}

	fs::path GetBaseTitlePath()
	{
		return sBasePath;
	}

	fs::path GetUpdateTitlePath()
	{
		return sUpdatePath;
	}

	fs::path GetAocTitlePath()
	{
		return sAocPath;
	}

	uint32 GetUpdateVersion()
	{
		return sUpdateVersion;
	}

	bool IsGameAvailable()
	{
		return !sBasePath.empty();
	}

	void FatalError(const std::string& message)
	{
		std::wstring w = boost::nowide::widen(message);
		MessageBoxW(nullptr, w.c_str(), boost::nowide::widen(kGameTitle).c_str(), MB_OK | MB_ICONERROR);
	}
}

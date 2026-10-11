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
#include <zarchive/zarchivereader.h>

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

		// A title of the game: a folder with code / content / meta, or such a folder inside a Wii U archive (.wua). An
		// archive holds one folder per title in its root, named <title id>_v<version>.
		struct GameTitle
		{
			uint64 titleId = 0;
			uint32 version = 0;
			fs::path path;       // title folder, or the archive file
			std::string subPath; // folder of the title inside the archive (empty for a title folder)
			bool empty() const { return path.empty(); }
			std::string describe() const { return subPath.empty() ? _pathToUtf8(path) : _pathToUtf8(path) + " [" + subPath + "]"; }
		};
		GameTitle sBase, sUpdate, sAoc;

		// archives that are in use stay open (their file table is large)
		std::mutex sArchiveMutex;
		std::map<fs::path, std::unique_ptr<ZArchiveReader>> sArchives;

		bool IsArchive(const fs::path& p)
		{
			std::error_code ec;
			return boost::iequals(_pathToUtf8(p.extension()), ".wua") && fs::is_regular_file(p, ec);
		}

		bool ReadArchiveFile(ZArchiveReader* zar, const std::string& path, std::vector<uint8>& out)
		{
			const ZArchiveNodeHandle node = zar->LookUp(path, true, false);
			if (node == ZARCHIVE_INVALID_NODE)
				return false;
			const uint64 size = zar->GetFileSize(node);
			if (size > 0x10000000ull)
				return false;
			out.resize((size_t)size);
			return zar->ReadFromFile(node, 0, size, out.data()) == size;
		}

		// size of a file of a title, -1 if the title does not have it
		sint64 TitleFileSize(const GameTitle& t, std::string_view rel)
		{
			if (t.empty())
				return -1;
			std::error_code ec;
			if (t.subPath.empty())
			{
				const uint64 size = fs::file_size(t.path / _utf8ToPath(rel), ec);
				return ec ? -1 : (sint64)size;
			}
			std::lock_guard lock(sArchiveMutex);
			auto& zar = sArchives[t.path];
			if (!zar)
				zar.reset(ZArchiveReader::OpenFromFile(t.path));
			if (!zar)
				return -1;
			const ZArchiveNodeHandle node = zar->LookUp(t.subPath + "/" + std::string(rel), true, false);
			return node == ZARCHIVE_INVALID_NODE ? -1 : (sint64)zar->GetFileSize(node);
		}

		bool ReadTitleFile(const GameTitle& t, std::string_view rel, std::vector<uint8>& out)
		{
			if (t.empty())
				return false;
			if (t.subPath.empty())
			{
				std::ifstream f(t.path / _utf8ToPath(rel), std::ios::binary);
				if (!f)
					return false;
				out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
				return true;
			}
			std::lock_guard lock(sArchiveMutex);
			auto& zar = sArchives[t.path];
			if (!zar)
				zar.reset(ZArchiveReader::OpenFromFile(t.path));
			return zar && ReadArchiveFile(zar.get(), t.subPath + "/" + std::string(rel), out);
		}
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

		// title id and title version from the contents of a code/app.xml
		bool ParseAppXml(const void* data, size_t size, GameTitle& t)
		{
			pugi::xml_document doc;
			if (!doc.load_buffer(data, size))
				return false;
			auto node = doc.child("app").child("title_id");
			if (!node)
				return false;
			try
			{
				t.titleId = std::stoull(node.text().as_string(), nullptr, 16);
				t.version = (uint32)std::stoul(doc.child("app").child("title_version").text().as_string("0"), nullptr, 16);
			}
			catch (const std::exception&)
			{
				return false;
			}
			return true;
		}

		// a title folder (code/app.xml tells which title it is)
		std::optional<GameTitle> ReadLooseTitle(const fs::path& titleDir)
		{
			std::ifstream f(titleDir / "code" / "app.xml", std::ios::binary);
			if (!f)
				return std::nullopt;
			const std::vector<char> xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			GameTitle t;
			if (!ParseAppXml(xml.data(), xml.size(), t))
				return std::nullopt;
			t.path = titleDir;
			return t;
		}

		// the game's titles inside a Wii U archive
		void CollectArchiveTitles(const fs::path& file, std::vector<GameTitle>& out)
		{
			std::unique_ptr<ZArchiveReader> zar(ZArchiveReader::OpenFromFile(file));
			if (!zar)
				return;
			const ZArchiveNodeHandle root = zar->LookUp("", false, true);
			if (root == ZARCHIVE_INVALID_NODE)
				return;
			for (uint32 i = 0; i < zar->GetDirEntryCount(root); i++)
			{
				ZArchiveReader::DirEntry e;
				if (!zar->GetDirEntry(root, i, e) || !e.isDirectory)
					continue;
				GameTitle t;
				std::vector<uint8> xml;
				if (!ReadArchiveFile(zar.get(), std::string(e.name) + "/code/app.xml", xml) || !ParseAppXml(xml.data(), xml.size(), t))
					continue;
				t.path = file;
				t.subPath = e.name;
				if (IsGameRegion((uint32)t.titleId))
					out.push_back(t);
			}
		}

		// collect the game's titles below a folder (limited depth; title folders are not searched further). The folder
		// may also be an archive file itself.
		void CollectTitles(const fs::path& dir, int depth, std::vector<GameTitle>& out)
		{
			std::error_code ec;
			if (IsArchive(dir))
			{
				CollectArchiveTitles(dir, out);
				return;
			}
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
				if (e.is_directory(ec) || IsArchive(e.path()))
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
			std::vector<GameTitle> titles;
			CollectTitles(root, 3, titles);
			// the base game itself was given (folder or archive): update and DLC are usually right next to it
			if (ReadLooseTitle(root) || IsArchive(root))
				CollectTitles(root.parent_path(), 1, titles);
			const GameTitle* base = nullptr;
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
			auto find = [&](uint32 high) -> const GameTitle* {
				for (auto& t : titles)
				{
					if ((uint32)(t.titleId >> 32) == high && (uint32)t.titleId == region)
						return &t;
				}
				return nullptr;
			};
			sBase = *base;
			sRegion = region;
			sUpdate = {};
			sAoc = {};
			sUpdateVersion = 0;
			if (auto u = find(kHighUpdate))
			{
				sUpdate = *u;
				sUpdateVersion = u->version;
			}
			if (auto a = find(kHighAoc))
				sAoc = *a;
			return true;
		}

		// The update as a source of files. An update that is installed in our own title folder is used as well.
		GameTitle UpdateForFiles()
		{
			if (!sUpdate.empty())
				return sUpdate;
			GameTitle t;
			std::error_code ec;
			for (const char* region : {"101c9500", "101c9400", "101c9300"})
			{
				const fs::path p = GetDataDir() / "mlc01" / "usr" / "title" / "0005000e" / region;
				if (fs::exists(p / "content" / "Font", ec))
				{
					t.path = p;
					break;
				}
			}
			return t;
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
			// Cemu's title list gets exactly the game's own title folders (nothing else is scanned). Titles in an archive
			// are registered at launch (GetGameArchives).
			c.game_paths.clear();
			for (const GameTitle* t : {&sBase, &sUpdate, &sAoc})
			{
				if (!t->empty() && t->subPath.empty())
					c.game_paths.emplace_back(_pathToUtf8(t->path));
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
			std::vector<uint8> tga;
			return LocateGame(fs::path(args[i + 1])) && ReadTitleFile(sBase, "meta/iconTex.tga", tga) && Icon::WriteFromTga(tga, fs::path(args[i + 2])) ? 0 : 1;
		}
		for (size_t i = 0; i < args.size(); i++)
		{
			if (!boost::iequals(args[i], L"--check-game"))
				continue;
			if (i + 1 >= args.size() || !LocateGame(fs::path(args[i + 1])))
				return 0;
			return 1 | (sUpdate.empty() ? 0 : 2) | (sUpdateVersion == kRequiredUpdateVersion ? 4 : 0) | (sAoc.empty() ? 0 : 8);
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
			cemuLog_log(LogType::Force, "PCE: game found at {} (update: {}, version {}; DLC: {})", sBase.describe(),
						sUpdate.empty() ? "not found" : sUpdate.describe(), sUpdateVersion, sAoc.empty() ? "not found" : sAoc.describe());
			UpdateSettings([&](Settings& s) { if (s.gamePath.empty()) s.gamePath = _pathToUtf8(sGameRoot); });
			// window / shortcut icon: made here from the dump's own icon, never shipped (the installer usually wrote it)
			std::error_code ec;
			if (std::vector<uint8> tga; !fs::exists(GetDataDir() / "game.ico", ec) && ReadTitleFile(sBase, "meta/iconTex.tga", tga))
				Icon::WriteFromTga(tga, GetDataDir() / "game.ico");
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
		return sBase.path;
	}

	fs::path GetUpdateTitlePath()
	{
		return sUpdate.path;
	}

	fs::path GetAocTitlePath()
	{
		return sAoc.path;
	}

	std::vector<fs::path> GetGameArchives()
	{
		std::vector<fs::path> r;
		for (const GameTitle* t : {&sUpdate, &sAoc})
		{
			if (!t->empty() && !t->subPath.empty() && std::find(r.begin(), r.end(), t->path) == r.end())
				r.push_back(t->path);
		}
		return r;
	}

	bool ReadGameFile(std::string_view rel, std::vector<uint8>& out)
	{
		const GameTitle update = UpdateForFiles();
		return (TitleFileSize(update, rel) >= 0 && ReadTitleFile(update, rel, out)) || ReadTitleFile(sBase, rel, out);
	}

	std::string GameFileStamp(std::string_view rel)
	{
		const GameTitle update = UpdateForFiles();
		for (const GameTitle* t : std::initializer_list<const GameTitle*>{&update, &sBase})
		{
			if (const sint64 size = TitleFileSize(*t, rel); size >= 0)
				return fmt::format("{}/{}:{}", t->describe(), rel, size);
		}
		return {};
	}

	uint32 GetUpdateVersion()
	{
		return sUpdateVersion;
	}

	bool IsGameAvailable()
	{
		return !sBase.empty();
	}

	void FatalError(const std::string& message)
	{
		std::wstring w = boost::nowide::widen(message);
		MessageBoxW(nullptr, w.c_str(), boost::nowide::widen(kGameTitle).c_str(), MB_OK | MB_ICONERROR);
	}
}

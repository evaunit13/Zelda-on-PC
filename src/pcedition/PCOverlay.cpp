#include "pcedition/PCOverlay.h"
#include "pcedition/PCSettings.h"
#include "pcedition/PCInput.h"
#include "pcedition/PCGraphics.h"
#include "pcedition/PCEdition.h"
#include "pcedition/PCMenus.h"
#include "pcedition/PCUI.h"
#include "pcedition/PCKeys.h"
#include "pcedition/PCPrompts.h"

#include <windows.h>
#include <fstream>
#include <thread>

#include "imgui.h"
#include "imgui_internal.h"
#include "config/ActiveSettings.h"
#include "Cafe/CafeSystem.h"
#include "gui/interface/WindowSystem.h"

void LatteFrameGen_GetStats(float& baseFps, float& shownFps, bool& generating); // LatteRenderTarget.cpp

namespace pce::Overlay
{
	namespace
	{
		std::atomic<bool> sOpen{false};
		float sAnim = 0.0f;
		double sLastTime = 0.0;

		// --- fonts ---
		std::vector<uint8> sFontData;
		struct FontSet
		{
			int height = 0;
			ImFont* title = nullptr;
			ImFont* item = nullptr;
			ImFont* small = nullptr;
			ImFont* key = nullptr;   // keycap labels of HUD prompts (bold, large: scaled down when drawn)
		} sFonts;
		std::vector<uint8> sKeyFontData;
		int sRequestedHeight = 0;

		// --- style (BotW inspired palette) ---
		const ImU32 kText = IM_COL32(237, 232, 208, 255);
		const ImU32 kTextDim = IM_COL32(160, 155, 140, 255);
		const ImU32 kSheikah = IM_COL32(110, 225, 255, 255);
		const ImU32 kSheikahDim = IM_COL32(110, 225, 255, 70);
		const ImU32 kWarn = IM_COL32(255, 120, 90, 255);
		const ImU32 kPanel = IM_COL32(8, 10, 12, 215);
		const ImU32 kBorder = IM_COL32(232, 225, 200, 150);

		enum class ItemType
		{
			Slider,
			Toggle,
			Choice,
			Binding,
			Button,
			Header
		};

		struct Item
		{
			ItemType type;
			std::string label;
			std::string help;
			float* fval = nullptr;
			float fmin = 0, fmax = 1, fstep = 0.1f;
			const char* fmt = "%.2f";
			bool* bval = nullptr;
			int* ival = nullptr;
			std::vector<std::string> options;
			Action action = Action::Count;
			std::function<void()> onPress;
			bool restart = false;
			bool rebuild = false; // other items depend on this one: the pages are built again after a change
		};

		struct Page
		{
			std::string name;
			std::vector<Item> items;
		};

		Settings sEdit; // working copy
		int sPage = 0;
		int sSel = 0;
		float sScroll = 0.0f;
		bool sRebinding = false;
		int sRebindSlot = 0;
		Action sRebindAction = Action::Count;
		double sRebindStart = 0.0;
		std::string sToast;
		double sToastTime = 0.0;
		bool sConfirmQuit = false;
		bool sMouseWasDown = false;
		int sFpsIndex = 1;
		int sLanguageIndex = 0; // into sLanguages
		std::vector<GameLanguage> sLanguages;
		const int kFpsValues[] = {30, 60, 90, 120, 144, 165, 240};

		double Now()
		{
			return (double)GetTickCount64() / 1000.0;
		}

		void Toast(const std::string& t)
		{
			sToast = t;
			sToastTime = Now();
		}

		void Commit()
		{
			Settings copy = sEdit;
			UpdateSettings([&](Settings& s) { s = copy; });
			Graphics::ApplyLive();
		}

		std::vector<Page> BuildPages()
		{
			std::vector<Page> pages;
			auto slider = [](const char* label, float* v, float mn, float mx, float step, const char* fmt, const char* help) {
				Item it{ItemType::Slider, label, help};
				it.fval = v;
				it.fmin = mn;
				it.fmax = mx;
				it.fstep = step;
				it.fmt = fmt;
				return it;
			};
			auto toggle = [](const char* label, bool* v, const char* help) {
				Item it{ItemType::Toggle, label, help};
				it.bval = v;
				return it;
			};
			auto choice = [](const char* label, int* v, std::vector<std::string> opts, const char* help, bool restart = false) {
				Item it{ItemType::Choice, label, help};
				it.ival = v;
				it.options = std::move(opts);
				it.restart = restart;
				return it;
			};
			auto header = [](const char* label) { return Item{ItemType::Header, label, ""}; };
			auto button = [](const char* label, const char* help, std::function<void()> fn) {
				Item it{ItemType::Button, label, help};
				it.onPress = std::move(fn);
				return it;
			};

			{
				Page p{"Camera"};
				p.items.push_back(slider("Mouse Sensitivity", &sEdit.mouseSensitivity, 0.05f, 6.0f, 0.05f, "%.2f", "Camera rotation per mouse movement: 1:1, no acceleration. Same scale as Source engine games (0.022 degrees per count)."));
				p.items.push_back(slider("Vertical Sensitivity", &sEdit.mouseSensitivityY, 0.25f, 2.0f, 0.05f, "%.2fx", "Vertical sensitivity relative to horizontal."));
				p.items.push_back(slider("Aiming Sensitivity", &sEdit.aimSensitivity, 0.1f, 2.0f, 0.05f, "%.2fx", "Sensitivity multiplier while drawing the bow or using the scope."));
				p.items.push_back(toggle("Invert Horizontal", &sEdit.invertX, "Invert left / right camera movement."));
				p.items.push_back(toggle("Invert Vertical", &sEdit.invertY, "Invert up / down camera movement."));
				p.items.push_back(slider("Keyboard Camera Speed", &sEdit.keyCameraSpeed, 0.1f, 1.0f, 0.05f, "%.2f", "Speed of the optional keyboard camera keys."));
				p.items.push_back(slider("Walk Speed", &sEdit.walkSpeed, 0.2f, 0.8f, 0.05f, "%.2f", "Movement speed while the Walk key is held."));
				p.items.push_back(toggle("Parry with Attack", &sEdit.parryWithAttack, "While the shield is raised (Lock On held), the Attack key performs a perfect guard."));
				p.items.push_back(toggle("Toggle Sprint", &sEdit.toggleSprint, "On: press the Sprint key once to keep sprinting while you move. Press it again or stand still to stop. Off: hold the key."));
				p.items.push_back(toggle("Toggle Lock On / Shield", &sEdit.toggleShield, "On: press the Lock On key once to keep the shield raised and the target locked, press it again to lower it. Off: hold the key. Crouching is always a toggle in the game."));
				p.items.push_back(slider("Weapon Wheel Delay", &sEdit.wheelReleaseDelayMs, 150.0f, 1200.0f, 25.0f, "%.0f ms", "How long the weapon selector stays open after the last mouse wheel step."));
				p.items.push_back(toggle("Selectors: Move Keys", &sEdit.selectorMoveKeys, "On: while a weapon, bow, shield or rune selector is open, the Move Left / Move Right keys step through it. The mouse and the wheel keep working."));
				p.items.push_back(toggle("Inventory: Move Keys", &sEdit.inventoryMoveKeys, "On: Move Left / Move Right move the cursor in the inventory, like Move Forward / Back do. The pages (Adventure Log, Inventory, System) are then switched with the Inventory: Previous / Next Page keys (Controls). Off: Move Left / Move Right switch the pages."));
				pages.push_back(std::move(p));
			}
			{
				Page p{"Controls"};
				const char* lastGroup = "";
				for (size_t i = 0; i < kActionCount; i++)
				{
					const auto& info = GetActionInfo((Action)i);
					if (strcmp(info.group, lastGroup) != 0)
					{
						p.items.push_back(header(info.group));
						lastGroup = info.group;
					}
					Item it{ItemType::Binding, info.name, "Enter: change key   Del: clear   Two keys can be assigned to every action."};
					it.action = (Action)i;
					p.items.push_back(it);
				}
				p.items.push_back(button("Reset All Controls", "Restore the default key assignments.", []() {
					sEdit.ResetBindings();
					Commit();
					Toast("Controls reset to default");
				}));
				pages.push_back(std::move(p));
			}
			{
				Page p{"Motion"};
				p.items.push_back(slider("Tilt Sensitivity", &sEdit.tiltSensitivity, 0.1f, 5.0f, 0.05f, "%.2f", "How much the mouse tilts the Sheikah Slate while Motion Control is active."));
				p.items.push_back(toggle("Toggle Mode", &sEdit.tiltToggle, "ON: the Motion Control key toggles tilting. OFF: hold the key to tilt."));
				p.items.push_back(toggle("Invert Tilt Left / Right", &sEdit.tiltInvertX, ""));
				p.items.push_back(toggle("Invert Tilt Forward / Back", &sEdit.tiltInvertY, ""));
				p.items.push_back(slider("Maximum Tilt", &sEdit.tiltMaxAngle, 15.0f, 90.0f, 1.0f, "%.0f\xC2\xB0", "Largest tilt angle reachable with the mouse."));
				p.items.push_back(slider("Auto Level Speed", &sEdit.tiltReturnSpeed, 0.0f, 120.0f, 5.0f, "%.0f\xC2\xB0/s", "Slowly levels the slate when the mouse rests. 0 = off."));
				pages.push_back(std::move(p));
			}
			{
				Page p{"Graphics"};
				{
					Item it = choice("Aspect Ratio", &sEdit.aspectRatio, Graphics::GetAspectOptions(),
									 "Shape of the picture. Automatic: 21:9 or 32:9 on such a display, 16:9 on every other. With another "
									 "shape than the window's, the picture gets bars. Applied on next launch.", true);
					it.rebuild = true;
					p.items.push_back(it);
				}
				const int aspect = Graphics::EffectiveAspect(sEdit.aspectRatio);
				std::vector<std::string> res;
				for (auto& r : Graphics::GetResolutionOptions(aspect))
					res.push_back(r.label);
				p.items.push_back(choice("Resolution", &sEdit.resolutionPreset, res, "Internal rendering resolution. Applied on next launch.", true));
				if (aspect >= 3)
					p.items.push_back(choice("Ultrawide HUD", &sEdit.ultrawideHud, {"At the screen edges", "Centred"},
											 "Where hearts, map and prompts are on an ultrawide picture: at its edges, or within a 16:9 area "
											 "in the middle. Applied on next launch.", true));
				p.items.push_back(choice("Frame Rate", &sFpsIndex, {"30 FPS", "60 FPS", "90 FPS", "120 FPS", "144 FPS", "165 FPS", "240 FPS"},
										 "Frame rate limit. Game speed and physics are corrected automatically. Above 60 FPS, turn V-Sync off "
										 "unless your display runs at that refresh rate."));
				p.items.push_back(toggle("V-Sync", &sEdit.vsync, "Synchronize with the display refresh rate."));
				p.items.push_back(choice("Frame Generation", &sEdit.frameGeneration, {"Off", "AMD FSR 3"},
										 "AMD FSR 3: a generated frame between every two real ones. The Frame Rate limit then counts the frames "
										 "you see: the game runs at half of it (120 FPS = 60 real + 60 generated). Adds about one game frame of "
										 "input lag. Game logic and physics are never affected."));
				p.items.push_back(toggle("CPU Thread Pinning", &sEdit.cpuPinning,
										 "Experimental. Ties the emulated Wii U CPU cores and the GPU thread to fixed processor cores that share one "
										 "cache. A small gain on some CPUs, a large loss on others (Ryzen X3D with two cache groups): leave it off "
										 "unless it helps on yours."));
				p.items.push_back(toggle("Show FPS", &sEdit.showFps, "Shows the game frame rate and, with Frame Generation, the displayed frame rate."));
				p.items.push_back(choice("Anti-Aliasing", &sEdit.antiAliasing, Graphics::GetAAOptions(), ""));
				p.items.push_back(choice("Shadow Quality", &sEdit.shadowPreset, Graphics::GetShadowOptions(), "Shadow resolution and distance. Applied on next launch.", true));
				pages.push_back(std::move(p));
			}
			{
				Page p{"Game"};
				p.items.push_back(button("Resume", "Return to the game.", []() { SetOpen(false); }));
				p.items.push_back(button("Save Game", "Opens the game's save prompt.", []() {
					SetOpen(false);
					Menus::RequestSave();
				}));
				p.items.push_back(button("Load Game", "Opens the list of saves.", []() {
					SetOpen(false);
					Menus::RequestLoad();
				}));
				if (sLanguages.size() > 1)
				{
					std::vector<std::string> names;
					for (const GameLanguage& l : sLanguages)
						names.emplace_back(l.name);
					p.items.push_back(choice("Language", &sLanguageIndex, names,
											 "Language of the game's texts (the languages your copy of the game has). The voices are chosen in the game's own "
											 "Options on the title screen. Applied on next launch.",
											 true));
				}
				p.items.push_back(choice("Temperature Unit", &sEdit.temperatureUnit, {"Automatic", "Celsius", "Fahrenheit"},
										 "Unit of the temperature on the map. Automatic: as the game decides (Fahrenheit for US English, Celsius "
										 "otherwise)."));
				p.items.push_back(button("Quit to Desktop", "Unsaved progress since the last save will be lost.", []() { sConfirmQuit = true; }));
				pages.push_back(std::move(p));
			}
			return pages;
		}

		std::vector<Page> sPages;

		bool Selectable(const Item& it)
		{
			return it.type != ItemType::Header;
		}

		void FixSelection(int dir)
		{
			auto& items = sPages[sPage].items;
			if (items.empty())
				return;
			sSel = std::clamp(sSel, 0, (int)items.size() - 1);
			int guard = 0;
			while (!Selectable(items[sSel]) && guard++ < (int)items.size())
			{
				sSel += dir == 0 ? 1 : dir;
				if (sSel >= (int)items.size()) sSel = 0;
				if (sSel < 0) sSel = (int)items.size() - 1;
			}
		}

		bool KeyHasConflict(Action a, Key k)
		{
			if (k == Key::None)
				return false;
			for (size_t i = 0; i < kActionCount; i++)
			{
				if ((Action)i == a)
					continue;
				for (Key o : sEdit.bindings[i].keys)
					if (o == k)
						return true;
			}
			return false;
		}

		std::string ConflictName(Action a, Key k)
		{
			for (size_t i = 0; i < kActionCount; i++)
			{
				if ((Action)i == a)
					continue;
				for (Key o : sEdit.bindings[i].keys)
					if (o == k)
						return GetActionInfo((Action)i).name;
			}
			return "";
		}

		void Adjust(Item& it, int dir)
		{
			switch (it.type)
			{
			case ItemType::Slider:
				*it.fval = std::clamp(*it.fval + dir * it.fstep, it.fmin, it.fmax);
				*it.fval = roundf(*it.fval / it.fstep) * it.fstep;
				break;
			case ItemType::Toggle:
				*it.bval = !*it.bval;
				break;
			case ItemType::Choice:
				*it.ival = std::clamp(*it.ival + dir, 0, (int)it.options.size() - 1);
				break;
			default:
				return;
			}
			sEdit.fpsLimit = kFpsValues[std::clamp(sFpsIndex, 0, (int)std::size(kFpsValues) - 1)];
			if (!sLanguages.empty())
				sEdit.language = sLanguages[std::clamp(sLanguageIndex, 0, (int)sLanguages.size() - 1)].code;
			Commit();
			if (it.restart)
				Toast("This change takes effect the next time the game starts");
			if (it.rebuild)
			{
				// (the item is gone after this: nothing may use it any more)
				const int selected = sSel;
				sPages = BuildPages();
				sSel = std::clamp(selected, 0, std::max(0, (int)sPages[sPage].items.size() - 1));
			}
		}

		void Activate(Item& it)
		{
			switch (it.type)
			{
			case ItemType::Toggle:
				Adjust(it, 1);
				break;
			case ItemType::Choice:
				*it.ival = (*it.ival + 1) % (int)it.options.size();
				Adjust(it, 0);
				break;
			case ItemType::Binding:
				sRebinding = true;
				sRebindAction = it.action;
				sRebindStart = Now();
				Input::ClearKeyPresses();
				break;
			case ItemType::Button:
				if (it.onPress)
					it.onPress();
				break;
			default:
				break;
			}
		}

		void FinishRebind(Key k)
		{
			auto& b = sEdit.bind(sRebindAction).keys;
			if (k == b[sRebindSlot ^ 1])
				b[sRebindSlot ^ 1] = Key::None; // same key in both slots -> keep one
			b[sRebindSlot] = k;
			sRebinding = false;
			if (KeyHasConflict(sRebindAction, k))
				Toast(fmt::format("{} is also used by \"{}\"", KeyDisplayName(k), ConflictName(sRebindAction, k)));
			Commit();
		}

		// ------------- drawing helpers -------------
		void TextAligned(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const std::string& s, float alignX = 0.0f)
		{
			ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str());
			dl->AddText(f, size, ImVec2(pos.x - sz.x * alignX, pos.y), col, s.c_str());
		}

		void KeyCap(ImDrawList* dl, ImFont* f, float size, ImVec2 center, Key k, ImU32 col, float alpha)
		{
			std::string label = k == Key::None ? "-" : KeyDisplayName(k);
			ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
			float w = std::max(ts.x + size * 0.9f, size * 1.6f), h = size * 1.35f;
			ImVec2 a(center.x - w * 0.5f, center.y - h * 0.5f), b(center.x + w * 0.5f, center.y + h * 0.5f);
			if (k != Key::None)
			{
				dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (int)(18 * alpha)), h * 0.22f);
				dl->AddRect(a, b, (col & 0x00FFFFFF) | ((uint32)(150 * alpha) << 24), h * 0.22f, 0, std::max(1.0f, size * 0.07f));
			}
			dl->AddText(f, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col, label.c_str());
		}

		void SheikahEye(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float t)
		{
			// stylized eye: almond + iris + tear drop, drawn with lines
			const int n = 32;
			ImVec2 top[33], bot[33];
			for (int i = 0; i <= n; i++)
			{
				float x = -1.0f + 2.0f * i / n;
				float y = 0.55f * (1.0f - x * x);
				top[i] = ImVec2(c.x + x * r, c.y - y * r);
				bot[i] = ImVec2(c.x + x * r, c.y + y * r);
			}
			dl->AddPolyline(top, n + 1, col, 0, r * 0.08f);
			dl->AddPolyline(bot, n + 1, col, 0, r * 0.08f);
			dl->AddCircle(c, r * 0.32f, col, 24, r * 0.08f);
			dl->AddCircleFilled(c, r * 0.12f, col, 16);
			ImVec2 tear[4] = {ImVec2(c.x, c.y + r * 0.62f), ImVec2(c.x - r * 0.12f, c.y + r * 0.85f), ImVec2(c.x, c.y + r * 1.05f), ImVec2(c.x + r * 0.12f, c.y + r * 0.85f)};
			dl->AddConvexPolyFilled(tear, 4, col);
			for (int i = 0; i < 3; i++)
			{
				float a = -1.5708f + (i - 1) * 0.55f;
				ImVec2 p0(c.x + cosf(a) * r * 0.7f, c.y + sinf(a) * r * 0.7f), p1(c.x + cosf(a) * r * 0.95f, c.y + sinf(a) * r * 0.95f);
				dl->AddLine(p0, p1, col, r * 0.07f);
			}
		}
	}

	void DebugSetPage(int page)
	{
		sPage = std::clamp(page, 0, std::max(0, (int)sPages.size() - 1));
		sSel = 0;
		sScroll = 0;
	}

	void SetOpen(bool open)
	{
		if (open == sOpen.load())
			return;
		if (open)
		{
			sEdit = GetSettings();
			sFpsIndex = 0;
			for (int i = 0; i < (int)std::size(kFpsValues); i++)
				if (sEdit.fpsLimit >= kFpsValues[i])
					sFpsIndex = i;
			sLanguages = GetGameLanguages();
			sLanguageIndex = 0;
			for (int i = 0; i < (int)sLanguages.size(); i++)
				if (sEdit.language == sLanguages[i].code)
					sLanguageIndex = i;
			sPages = BuildPages();
			sRebinding = false;
			sConfirmQuit = false;
			FixSelection(1);
		}
		sOpen = open;
		Input::SetOverlayOpen(open);
	}

	bool IsOpen()
	{
		return sOpen.load();
	}

	void Toggle()
	{
		SetOpen(!IsOpen());
	}

	bool WantsPause()
	{
		return sOpen.load();
	}

	namespace
	{
		std::atomic<bool> sGamePaused{false};
		std::atomic<bool> sShutdown{false};
	}

	void BlockWhileOpen()
	{
		if (!sOpen.load() || sShutdown.load())
			return;
		sGamePaused = true;
		while (sOpen.load() && !sShutdown.load())
			std::this_thread::sleep_for(std::chrono::milliseconds(4));
		sGamePaused = false;
	}

	bool IsGamePaused()
	{
		return sGamePaused.load();
	}

	void DrawLoadingIndicator(float progress)
	{
		ImGuiIO& io = ImGui::GetIO();
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		const float r = H * 0.022f;
		const ImVec2 c(W - r * 3.2f, H - r * 2.6f);
		const float t = (float)Now();
		const float pulse = 0.55f + 0.45f * sinf(t * 3.0f);
		SheikahEye(dl, c, r, IM_COL32(110, 225, 255, (int)(255 * pulse)), t);
		// thin progress arc around the eye
		const float a0 = -1.5708f, a1 = a0 + 6.28318f * std::clamp(progress, 0.0f, 1.0f);
		dl->PathArcTo(c, r * 1.55f, a0, a1, 48);
		dl->PathStroke(IM_COL32(110, 225, 255, 160), 0, std::max(1.5f, r * 0.08f));
	}

	void ReleaseForShutdown()
	{
		sShutdown = true;
		sOpen = false;
		Input::SetOverlayOpen(false);
	}

	void PrepareFrame()
	{
		ImGuiIO& io = ImGui::GetIO();
		int h = (int)io.DisplaySize.y;
		if (h <= 0)
			return;
		int fh = std::max(12, h / 30);
		if (sFonts.height == fh && sFonts.item)
			return;
		if (sFontData.empty())
		{
			fs::path p = ActiveSettings::GetDataPath("resources/sharedFonts/CafeStd.ttf");
			std::ifstream f(p, std::ios::binary);
			sFontData.assign(std::istreambuf_iterator<char>(f), {});
			if (sFontData.empty())
				return;
		}
		ImFontConfig cfg{};
		cfg.FontDataOwnedByAtlas = false;
		static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2190, 0x2193, 0x25B6, 0x25C0, 0};
		sFonts.title = io.Fonts->AddFontFromMemoryTTF(sFontData.data(), (int)sFontData.size(), (float)fh * 1.45f, &cfg, ranges);
		sFonts.item = io.Fonts->AddFontFromMemoryTTF(sFontData.data(), (int)sFontData.size(), (float)fh, &cfg, ranges);
		sFonts.small = io.Fonts->AddFontFromMemoryTTF(sFontData.data(), (int)sFontData.size(), (float)fh * 0.72f, &cfg, ranges);
		if (sKeyFontData.empty())
		{
			wchar_t windir[MAX_PATH]{};
			GetWindowsDirectoryW(windir, MAX_PATH);
			std::ifstream kf(fs::path(windir) / "Fonts" / "segoeuib.ttf", std::ios::binary);
			sKeyFontData.assign(std::istreambuf_iterator<char>(kf), {});
		}
		static const ImWchar keyRanges[] = {0x0020, 0x007E, 0};
		sFonts.key = sKeyFontData.empty() ? sFonts.item
										  : io.Fonts->AddFontFromMemoryTTF(sKeyFontData.data(), (int)sKeyFontData.size(), (float)fh * 1.2f, &cfg, keyRanges);
		sFonts.height = fh;
	}

	namespace
	{
		// a PC key / mouse button in the style of BotW's button circles: white cap, dark label
		void DrawPromptKey(ImDrawList* dl, const ImVec2& center, float h, uint16 glyph, float alpha)
		{
			const int a = (int)(255 * std::clamp(alpha, 0.0f, 1.0f));
			const ImU32 cap = IM_COL32(255, 255, 255, a);
			const ImU32 ink = IM_COL32(28, 28, 26, a);
			if (glyph < 0xE100)
				return;
			const Key k = (Key)(glyph - 0xE100);
			if (IsMouseKey(k) && k != Key::WheelUp && k != Key::WheelDown)
			{
				// mouse: body outline, the pressed button filled
				const float w = h * 0.7f, r = w * 0.5f, t = std::max(1.5f, h * 0.085f);
				const ImVec2 p0(center.x - w * 0.5f, center.y - h * 0.5f), p1(center.x + w * 0.5f, center.y + h * 0.5f);
				const float split = p0.y + h * 0.42f;
				dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, a / 3), r);
				if (k == Key::MouseLeft)
					dl->AddRectFilled(p0, ImVec2(center.x, split), cap, r, ImDrawFlags_RoundCornersTopLeft);
				if (k == Key::MouseRight)
					dl->AddRectFilled(ImVec2(center.x, p0.y), ImVec2(p1.x, split), cap, r, ImDrawFlags_RoundCornersTopRight);
				dl->AddRect(p0, p1, cap, r, 0, t);
				dl->AddLine(ImVec2(p0.x, split), ImVec2(p1.x, split), cap, t);
				dl->AddLine(ImVec2(center.x, p0.y), ImVec2(center.x, split), cap, t);
				if (k == Key::MouseMiddle)
					dl->AddRectFilled(ImVec2(center.x - w * 0.13f, p0.y + h * 0.12f), ImVec2(center.x + w * 0.13f, split + h * 0.08f), cap, w * 0.13f);
				if (k == Key::Mouse4 || k == Key::Mouse5)
				{
					const float y = split + h * (k == Key::Mouse4 ? 0.22f : 0.08f);
					dl->AddRectFilled(ImVec2(p0.x - w * 0.1f, y), ImVec2(p0.x + w * 0.22f, y + h * 0.13f), cap, 2.0f);
				}
				return;
			}
			std::string label = KeyCapLabel(k);
			if (k == Key::WheelUp)
				label = "Wheel+";
			else if (k == Key::WheelDown)
				label = "Wheel-";
			if (label.empty())
				return;
			ImFont* font = sFonts.key ? sFonts.key : ImGui::GetFont();
			float fs = h * (label.size() == 1 ? 0.80f : 0.48f);
			ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label.c_str());
			const float w = std::max(h, ts.x + h * 0.45f);
			const ImVec2 p0(center.x - w * 0.5f, center.y - h * 0.5f), p1(center.x + w * 0.5f, center.y + h * 0.5f);
			dl->AddRectFilled(p0, p1, cap, h * 0.22f);
			dl->AddText(font, fs, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.52f), ink, label.c_str());
		}

		void DrawFpsCounter()
		{
			static uint64 rev = ~0ull;
			static bool show = false;
			if (GetSettingsRevision() != rev)
			{
				rev = GetSettingsRevision();
				show = GetSettings().showFps;
			}
			if (!show)
				return;
			float base = 0.0f, shown = 0.0f;
			bool gen = false;
			LatteFrameGen_GetStats(base, shown, gen);
			const std::string text = gen ? fmt::format("{:.0f} FPS  |  {:.0f} displayed", base, shown) : fmt::format("{:.0f} FPS", base);
			ImDrawList* dl = ImGui::GetForegroundDrawList();
			const float fs = std::max(14.0f, ImGui::GetIO().DisplaySize.y * 0.018f);
			ImFont* font = ImGui::GetFont();
			const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text.c_str());
			const ImVec2 p(fs * 0.6f, fs * 0.5f);
			dl->AddRectFilled(ImVec2(p.x - fs * 0.35f, p.y - fs * 0.2f), ImVec2(p.x + ts.x + fs * 0.35f, p.y + ts.y + fs * 0.2f), IM_COL32(0, 0, 0, 140), fs * 0.3f);
			dl->AddText(font, fs, p, IM_COL32(235, 235, 220, 255), text.c_str());
		}

		// HUD action buttons (A/B/X/Y/L of the game's action diamond), see UI::GetHudPrompts
		void DrawHudPrompts()
		{
			if (!Prompts::Active())
				return;
			const auto prompts = UI::GetHudPrompts();
			if (prompts.empty())
				return;
			ImGuiIO& io = ImGui::GetIO();
			ImDrawList* dl = ImGui::GetBackgroundDrawList();
			// (the prompts are positions in the game's picture, which has bars in a window of another shape)
			float X = 0.0f, Y = 0.0f, W = io.DisplaySize.x, H = io.DisplaySize.y;
			int px, py, pw, ph, ww, wh;
			if (Graphics::GetPictureArea(px, py, pw, ph, ww, wh))
			{
				const float kx = io.DisplaySize.x / (float)ww, ky = io.DisplaySize.y / (float)wh;
				X = px * kx;
				Y = py * ky;
				W = pw * kx;
				H = ph * ky;
			}
			std::vector<ImVec2> drawn;
			for (const auto& p : prompts)
			{
				const ImVec2 c(X + (p.rect.x0 + p.rect.x1) * 0.5f * W, Y + (p.rect.y0 + p.rect.y1) * 0.5f * H);
				bool dup = false;
				for (auto& d : drawn)
					dup |= std::fabs(d.x - c.x) < 4.0f && std::fabs(d.y - c.y) < 4.0f;
				if (dup)
					continue;
				drawn.push_back(c);
				const float h = (p.rect.y1 - p.rect.y0) * H * 0.70f;
				DrawPromptKey(dl, c, h, p.glyph, p.alpha);
			}
		}
	}

	void Render()
	{
		DrawHudPrompts();
		DrawFpsCounter();
		const double now = Now();
		const float dt = sLastTime > 0 ? (float)std::min(0.1, now - sLastTime) : 0.016f;
		sLastTime = now;

		// global hotkey (works while playing): open/close the menu
		{
			Settings cur = GetSettings();
			// handled through key presses so that rebinding mode can capture keys
			if (!sOpen.load())
			{
				std::optional<Key> k;
				while ((k = Input::PopKeyPress()))
				{
					if ((*k == cur.bind(Action::Settings).keys[0] || *k == cur.bind(Action::Settings).keys[1]) && !Input::GameMenuOpen())
					{
						SetOpen(true);
						break;
					}
				}
			}
		}

		const float target = sOpen.load() ? 1.0f : 0.0f;
		sAnim += (target - sAnim) * std::min(1.0f, dt * 12.0f);
		if (fabs(sAnim - target) < 0.003f)
			sAnim = target;
		if (sAnim <= 0.0f || !sFonts.item)
			return;

		ImGuiIO& io = ImGui::GetIO();
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		const float a = sAnim;
		const float fs = (float)sFonts.height;
		auto alpha = [&](ImU32 c, float m = 1.0f) { return (c & 0x00FFFFFF) | ((uint32)(((c >> 24) & 0xFF) * a * m) << 24); };

		// ---------- input ----------
		if (sOpen.load())
		{
			auto& items = sPages[sPage].items;
			std::optional<Key> k;
			while ((k = Input::PopKeyPress()))
			{
				if (sConfirmQuit)
				{
					if (*k == Key::Enter || *k == KeyChar('Y'))
						PostMessageW((HWND)WindowSystem::GetWindowInfo().window_main.surface, WM_CLOSE, 0, 0);
					sConfirmQuit = false;
					continue;
				}
				if (sRebinding)
				{
					if (*k == Key::Escape)
					{
						sRebinding = false;
						continue;
					}
					if (*k == Key::MouseLeft && now - sRebindStart < 0.25)
						continue; // the click which started the rebinding
					FinishRebind(*k);
					continue;
				}
				Settings cur = GetSettings();
				const bool isSettingsKey = *k == cur.bind(Action::Settings).keys[0] || *k == cur.bind(Action::Settings).keys[1];
				if (*k == Key::Escape || *k == Key::Backspace || (isSettingsKey && *k != Key::MouseLeft))
				{
					SetOpen(false);
					break;
				}
				if (*k == Key::Up || *k == KeyChar('W'))
				{
					sSel--;
					if (sSel < 0) sSel = (int)items.size() - 1;
					FixSelection(-1);
				}
				else if (*k == Key::Down || *k == KeyChar('S'))
				{
					sSel++;
					if (sSel >= (int)items.size()) sSel = 0;
					FixSelection(1);
				}
				else if (*k == Key::Left || *k == KeyChar('A'))
				{
					if (!items.empty() && items[sSel].type == ItemType::Binding)
						sRebindSlot = 0;
					else if (!items.empty())
						Adjust(items[sSel], -1);
				}
				else if (*k == Key::Right || *k == KeyChar('D'))
				{
					if (!items.empty() && items[sSel].type == ItemType::Binding)
						sRebindSlot = 1;
					else if (!items.empty())
						Adjust(items[sSel], 1);
				}
				else if (*k == Key::Enter || *k == Key::Space)
				{
					if (!items.empty()) Activate(items[sSel]);
				}
				else if (*k == Key::Delete)
				{
					if (!items.empty() && items[sSel].type == ItemType::Binding)
					{
						sEdit.bind(items[sSel].action).keys[sRebindSlot] = Key::None;
						Commit();
					}
				}
				else if (*k == KeyChar('Q') || *k == Key::PageUp)
				{
					sPage = (sPage + (int)sPages.size() - 1) % (int)sPages.size();
					sSel = 0;
					sScroll = 0;
					FixSelection(1);
				}
				else if (*k == KeyChar('E') || *k == Key::PageDown || *k == Key::Tab)
				{
					sPage = (sPage + 1) % (int)sPages.size();
					sSel = 0;
					sScroll = 0;
					FixSelection(1);
				}

			}
		}
		const int wheel = Input::PopOverlayWheel();

		// ---------- background ----------
		dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, (int)(150 * a)));
		const float pw = std::min(W * 0.70f, H * 1.25f), ph = H * 0.78f;
		const ImVec2 p0((W - pw) * 0.5f, (H - ph) * 0.5f + (1.0f - a) * H * 0.03f), p1(p0.x + pw, p0.y + ph);
		dl->AddRectFilled(p0, p1, alpha(kPanel));
		dl->AddRect(p0, p1, alpha(kBorder), 0, 0, 1.5f);
		dl->AddRect(ImVec2(p0.x + 6, p0.y + 6), ImVec2(p1.x - 6, p1.y - 6), alpha(kBorder, 0.35f), 0, 0, 1.0f);

		// header
		SheikahEye(dl, ImVec2(p0.x + fs * 2.0f, p0.y + fs * 1.6f), fs * 0.9f, alpha(kSheikah), (float)now);
		dl->AddText(sFonts.title, fs * 1.45f, ImVec2(p0.x + fs * 3.6f, p0.y + fs * 0.85f), alpha(kText), "Settings");
		// tabs
		float tx = p1.x - fs * 1.0f;
		for (int i = (int)sPages.size() - 1; i >= 0; i--)
		{
			const std::string& n = sPages[i].name;
			ImVec2 ts = sFonts.item->CalcTextSizeA(fs, FLT_MAX, 0, n.c_str());
			ImVec2 tp(tx - ts.x, p0.y + fs * 1.05f);
			bool active = i == sPage;
			ImRect r(ImVec2(tp.x - fs * 0.4f, tp.y - fs * 0.3f), ImVec2(tp.x + ts.x + fs * 0.4f, tp.y + ts.y + fs * 0.4f));
			bool hover = sOpen && !sRebinding && r.Contains(io.MousePos);
			dl->AddText(sFonts.item, fs, tp, alpha(active ? kSheikah : (hover ? kText : kTextDim)), n.c_str());
			if (active)
				dl->AddLine(ImVec2(tp.x, tp.y + ts.y + fs * 0.15f), ImVec2(tp.x + ts.x, tp.y + ts.y + fs * 0.15f), alpha(kSheikah), 2.0f);
			if (hover && io.MouseDown[0] && !sMouseWasDown)
			{
				sPage = i;
				sSel = 0;
				sScroll = 0;
				FixSelection(1);
			}
			tx -= ts.x + fs * 1.4f;
		}
		const float headerBottom = p0.y + fs * 3.0f;
		dl->AddLine(ImVec2(p0.x + fs, headerBottom), ImVec2(p1.x - fs, headerBottom), alpha(kBorder, 0.6f), 1.0f);

		// list
		auto& page = sPages[sPage];
		auto& items = page.items;
		const float rowH = fs * 1.75f;
		const bool isControls = page.name == "Controls";
		const float listTop = headerBottom + fs * (isControls ? 1.7f : 0.6f);
		const float listBottom = p1.y - fs * 4.6f;
		const float listH = listBottom - listTop;
		const float contentH = rowH * items.size();
		// keep selection visible
		const float selY = sSel * rowH;
		if (selY - sScroll < 0) sScroll = selY;
		if (selY + rowH - sScroll > listH) sScroll = selY + rowH - listH;
		if (wheel != 0 && sOpen && !sRebinding)
			sScroll -= wheel * rowH * 2.0f;
		sScroll = std::clamp(sScroll, 0.0f, std::max(0.0f, contentH - listH));

		const float colLabel = p0.x + fs * 1.6f;
		const float colValue = p0.x + pw * 0.62f;
		const float colKey1 = p0.x + pw * 0.62f, colKey2 = p0.x + pw * 0.82f;
		dl->PushClipRect(ImVec2(p0.x, listTop), ImVec2(p1.x, listBottom), true);
		const bool mouseClicked = io.MouseDown[0] && !sMouseWasDown;
		for (int i = 0; i < (int)items.size(); i++)
		{
			Item& it = items[i];
			const float y = listTop + i * rowH - sScroll;
			if (y + rowH < listTop || y > listBottom)
				continue;
			ImVec2 r0(p0.x + fs * 0.8f, y), r1(p1.x - fs * 0.8f, y + rowH * 0.92f);
			const float ty = y + (rowH * 0.92f - fs) * 0.5f;
			if (it.type == ItemType::Header)
			{
				dl->AddText(sFonts.small, fs * 0.72f, ImVec2(colLabel, ty + fs * 0.2f), alpha(kSheikah, 0.9f), it.label.c_str());
				dl->AddLine(ImVec2(colLabel, y + rowH * 0.85f), ImVec2(r1.x - fs, y + rowH * 0.85f), alpha(kSheikahDim), 1.0f);
				continue;
			}
			const bool hover = sOpen && !sRebinding && !sConfirmQuit && ImRect(r0, r1).Contains(io.MousePos) && io.MousePos.y >= listTop && io.MousePos.y <= listBottom;
			if (hover && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || mouseClicked))
				sSel = i;
			const bool sel = i == sSel;
			if (sel)
			{
				dl->AddRectFilled(r0, r1, alpha(IM_COL32(110, 225, 255, 28)));
				dl->AddRect(r0, r1, alpha(kSheikah, 0.85f), 2.0f, 0, 1.5f);
				// glowing corner ticks
				float tk = fs * 0.5f;
				dl->AddLine(r0, ImVec2(r0.x + tk, r0.y), alpha(kSheikah), 3.0f);
				dl->AddLine(r0, ImVec2(r0.x, r0.y + tk), alpha(kSheikah), 3.0f);
				dl->AddLine(r1, ImVec2(r1.x - tk, r1.y), alpha(kSheikah), 3.0f);
				dl->AddLine(r1, ImVec2(r1.x, r1.y - tk), alpha(kSheikah), 3.0f);
			}
			dl->AddText(sFonts.item, fs, ImVec2(colLabel, ty), alpha(sel ? kText : kTextDim, sel ? 1.0f : 0.95f), it.label.c_str());
			switch (it.type)
			{
			case ItemType::Slider:
			{
				const float bx0 = colValue, bx1 = r1.x - fs * 5.0f;
				const float by = y + rowH * 0.46f;
				float t = (*it.fval - it.fmin) / (it.fmax - it.fmin);
				dl->AddLine(ImVec2(bx0, by), ImVec2(bx1, by), alpha(kBorder, 0.5f), 2.0f);
				dl->AddLine(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * t, by), alpha(kSheikah), 3.0f);
				dl->AddCircleFilled(ImVec2(bx0 + (bx1 - bx0) * t, by), fs * 0.28f, alpha(sel ? kSheikah : kText));
				char buf[64];
				snprintf(buf, sizeof(buf), it.fmt, *it.fval);
				TextAligned(dl, sFonts.item, fs, ImVec2(r1.x - fs * 0.6f, ty), alpha(kText), buf, 1.0f);
				if (sel && sOpen && io.MouseDown[0] && io.MousePos.x >= bx0 - fs && io.MousePos.x <= bx1 + fs && hover)
				{
					float nt = std::clamp((io.MousePos.x - bx0) / (bx1 - bx0), 0.0f, 1.0f);
					float nv = it.fmin + nt * (it.fmax - it.fmin);
					nv = roundf(nv / it.fstep) * it.fstep;
					if (nv != *it.fval)
					{
						*it.fval = nv;
						Commit();
					}
				}
				break;
			}
			case ItemType::Toggle:
			{
				const char* v = *it.bval ? "ON" : "OFF";
				TextAligned(dl, sFonts.item, fs, ImVec2(r1.x - fs * 0.6f, ty), alpha(*it.bval ? kSheikah : kText), v, 1.0f);
				if (sel)
					TextAligned(dl, sFonts.item, fs, ImVec2(r1.x - fs * 3.6f, ty), alpha(kTextDim), "\xE2\x97\x80", 1.0f);
				if (hover && mouseClicked)
					Adjust(it, 1);
				break;
			}
			case ItemType::Choice:
			{
				const std::string& v = it.options[std::clamp(*it.ival, 0, (int)it.options.size() - 1)];
				TextAligned(dl, sFonts.item, fs, ImVec2(r1.x - fs * 1.6f, ty), alpha(kText), v, 1.0f);
				if (sel)
				{
					ImVec2 vs = sFonts.item->CalcTextSizeA(fs, FLT_MAX, 0, v.c_str());
					TextAligned(dl, sFonts.item, fs * 0.8f, ImVec2(r1.x - fs * 2.4f - vs.x, ty + fs * 0.1f), alpha(kSheikah), "\xE2\x97\x80", 1.0f);
					TextAligned(dl, sFonts.item, fs * 0.8f, ImVec2(r1.x - fs * 0.6f, ty + fs * 0.1f), alpha(kSheikah), "\xE2\x96\xB6", 1.0f);
				}
				if (hover && mouseClicked)
					Adjust(it, io.MousePos.x < r1.x - fs * 6.0f ? -1 : 1);
				break;
			}
			case ItemType::Binding:
			{
				const auto& b = sEdit.bind(it.action).keys;
				for (int slot = 0; slot < 2; slot++)
				{
					const float cx = slot == 0 ? colKey1 + (colKey2 - colKey1) * 0.5f : colKey2 + (r1.x - colKey2) * 0.5f;
					const bool editing = sRebinding && sRebindAction == it.action && sRebindSlot == slot;
					const bool conflict = KeyHasConflict(it.action, b[slot]);
					ImU32 col = editing ? kSheikah : (conflict ? kWarn : (sel ? kText : kTextDim));
					if (editing)
					{
						const float pulse = 0.55f + 0.45f * sinf((float)now * 6.0f);
						TextAligned(dl, sFonts.small, fs * 0.72f, ImVec2(cx, ty + fs * 0.12f), alpha(kSheikah, pulse), "Press a key...", 0.5f);
					}
					else
						KeyCap(dl, sFonts.small, fs * 0.72f, ImVec2(cx, y + rowH * 0.46f), b[slot], alpha(col), a);
					if (sel && sRebindSlot == slot && !editing)
					{
						ImVec2 c(cx, y + rowH * 0.46f);
						dl->AddRect(ImVec2(c.x - (colKey2 - colKey1) * 0.45f, c.y - rowH * 0.38f), ImVec2(c.x + (colKey2 - colKey1) * 0.45f, c.y + rowH * 0.38f), alpha(kSheikah, 0.6f), 3.0f);
					}
					ImRect hr(ImVec2(cx - (colKey2 - colKey1) * 0.45f, y), ImVec2(cx + (colKey2 - colKey1) * 0.45f, y + rowH));
					if (hover && hr.Contains(io.MousePos) && mouseClicked)
					{
						sRebindSlot = slot;
						Activate(it);
					}
				}
				break;
			}
			case ItemType::Button:
				if (hover && mouseClicked)
					Activate(it);
				break;
			default:
				break;
			}
		}
		dl->PopClipRect();
		// column headings for controls
		if (isControls)
		{
			TextAligned(dl, sFonts.small, fs * 0.72f, ImVec2(colKey1 + (colKey2 - colKey1) * 0.5f, headerBottom + fs * 0.55f), alpha(kTextDim), "Primary", 0.5f);
			TextAligned(dl, sFonts.small, fs * 0.72f, ImVec2(colKey2 + (p1.x - fs * 0.8f - colKey2) * 0.5f, headerBottom + fs * 0.55f), alpha(kTextDim), "Secondary", 0.5f);
		}

		// description / help
		const float helpY = listBottom + fs * 0.5f;
		dl->AddLine(ImVec2(p0.x + fs, helpY), ImVec2(p1.x - fs, helpY), alpha(kBorder, 0.4f), 1.0f);
		if (!items.empty() && sSel < (int)items.size())
		{
			std::string help = items[sSel].help;
			if (items[sSel].type == ItemType::Binding)
			{
				const auto& b = sEdit.bind(items[sSel].action).keys;
				for (Key kk : b)
					if (KeyHasConflict(items[sSel].action, kk))
						help = fmt::format("Conflict: {} is also assigned to \"{}\".", KeyDisplayName(kk), ConflictName(items[sSel].action, kk));
			}
			dl->AddText(sFonts.small, fs * 0.8f, ImVec2(p0.x + fs * 1.6f, helpY + fs * 0.5f), alpha(kText, 0.9f), help.c_str());
		}
		if (Graphics::RestartPending())
			TextAligned(dl, sFonts.small, fs * 0.72f, ImVec2(p1.x - fs * 1.6f, helpY + fs * 0.55f), alpha(kSheikah, 0.9f), "Some changes apply after restarting", 1.0f);

		// guide bar
		const float gy = p1.y - fs * 1.7f;
		struct G { const char* key; const char* text; };
		const bool bindingRow = !items.empty() && items[sSel].type == ItemType::Binding;
		std::vector<G> guide = sRebinding ? std::vector<G>{{"Esc", "Cancel"}} : std::vector<G>{{"Enter", bindingRow ? "Change Key" : "Select"}, {"\xE2\x97\x80 \xE2\x96\xB6", bindingRow ? "Column" : "Change"}, {"Q / E", "Page"}, {"Esc", "Back"}};
		if (!sRebinding && bindingRow)
			guide.insert(guide.begin() + 1, {"Del", "Clear"});
		float gx = p1.x - fs * 1.2f;
		for (int i = (int)guide.size() - 1; i >= 0; i--)
		{
			ImVec2 ts = sFonts.small->CalcTextSizeA(fs * 0.8f, FLT_MAX, 0, guide[i].text);
			gx -= ts.x;
			dl->AddText(sFonts.small, fs * 0.8f, ImVec2(gx, gy), alpha(kText), guide[i].text);
			ImVec2 ks = sFonts.small->CalcTextSizeA(fs * 0.72f, FLT_MAX, 0, guide[i].key);
			float kw = ks.x + fs * 0.8f;
			gx -= kw + fs * 0.35f;
			ImVec2 k0(gx, gy - fs * 0.08f), k1(gx + kw, gy + fs * 0.92f);
			dl->AddRectFilled(k0, k1, alpha(IM_COL32(237, 232, 208, 235)), fs * 0.2f);
			dl->AddText(sFonts.small, fs * 0.72f, ImVec2(gx + fs * 0.4f, gy + fs * 0.05f), alpha(IM_COL32(20, 20, 20, 255)), guide[i].key);
			gx -= fs * 1.1f;
		}

		// toast
		if (!sToast.empty() && now - sToastTime < 3.0)
		{
			float ta = (float)std::min(1.0, (3.0 - (now - sToastTime)) * 2.0);
			ImVec2 ts = sFonts.small->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0, sToast.c_str());
			ImVec2 c(W * 0.5f, p0.y - fs * 1.2f);
			dl->AddRectFilled(ImVec2(c.x - ts.x * 0.5f - fs, c.y - fs * 0.5f), ImVec2(c.x + ts.x * 0.5f + fs, c.y + ts.y + fs * 0.4f), alpha(IM_COL32(0, 0, 0, 200), ta));
			dl->AddText(sFonts.small, fs * 0.85f, ImVec2(c.x - ts.x * 0.5f, c.y - fs * 0.05f), alpha(kText, ta), sToast.c_str());
		}

		// quit confirmation
		if (sConfirmQuit)
		{
			dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 120));
			ImVec2 c(W * 0.5f, H * 0.5f);
			ImVec2 b0(c.x - fs * 12, c.y - fs * 3), b1(c.x + fs * 12, c.y + fs * 3);
			dl->AddRectFilled(b0, b1, IM_COL32(8, 10, 12, 240));
			dl->AddRect(b0, b1, kBorder, 0, 0, 1.5f);
			TextAligned(dl, sFonts.item, fs, ImVec2(c.x, c.y - fs * 1.6f), kText, "Quit to desktop?", 0.5f);
			TextAligned(dl, sFonts.small, fs * 0.8f, ImVec2(c.x, c.y - fs * 0.2f), kTextDim, "Progress since your last save will be lost.", 0.5f);
			TextAligned(dl, sFonts.small, fs * 0.8f, ImVec2(c.x, c.y + fs * 1.4f), kSheikah, "Enter: Quit      Esc: Cancel", 0.5f);
		}
		sMouseWasDown = io.MouseDown[0];
	}
}

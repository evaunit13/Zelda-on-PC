#pragma once

// Zelda on PC - native keyboard & mouse input.
// A dedicated thread receives Windows Raw Input (mouse deltas, buttons, wheel, keyboard) and the game's
// VPADRead() is served directly from that state, bypassing Cemu's controller-profile system.

#include <atomic>
#include <optional>

#include "pcedition/PCKeys.h"
#include "pcedition/PCSettings.h"

struct VPADStatus;

namespace pce
{
	namespace Input
	{
		// registers the HLE functions called by the PC Edition camera patch (must run before the game is loaded)
		void RegisterHLE();
		void Start();
		void Shutdown();

		// set by the window system so we can test focus
		void SetMainWindow(void* hwnd);
		bool IsGameWindowFocused();

		// called from VPADRead (emulated CPU thread) once per game frame
		void FillVPAD(VPADStatus& status);
		void SetButtonRepeat(sint32 delayMs, sint32 pulseMs);

		// key state queries (for overlay)
		bool IsKeyDown(Key k);
		// returns the next key/button press since the last call (for rebinding & overlay navigation)
		std::optional<Key> PopKeyPress();
		void ClearKeyPresses();
		// accumulated wheel notches for the overlay while it is open
		int PopOverlayWheel();

		// overlay integration: while the overlay is open, game receives neutral input and the cursor is free
		void SetOverlayOpen(bool open);
		bool IsOverlayOpen();
		// true when the cursor should be hidden and clipped to the game window
		bool WantsCursorCaptured();
		// a game menu, the map or the scope is shown (Esc acts as Back there)
		bool GameMenuOpen();

		// dev bridge: a raw mouse movement as if it came from the device (goes through the same path as real input)
		void DebugRawMouse(sint32 dx, sint32 dy);

		// tilt (gyro) mode
		bool IsTiltActive();
		void SetTiltActive(bool active);

		// diagnostics (dev bridge)
		struct Debug
		{
			std::atomic<float> lastStickRX{0}, lastStickRY{0};
			std::atomic<float> lastDesiredYawDeg{0}, lastDesiredPitchDeg{0};
			std::atomic<uint32> frameCounter{0};
			std::atomic<bool> overrideRightStick{false};
			std::atomic<bool> forceFocus{false}; // dev: treat the game window as focused (mouse tests)
			std::atomic<float> overrideRX{0}, overrideRY{0};
			std::atomic<bool> overrideLeftStick{false};
			std::atomic<float> overrideLX{0}, overrideLY{0};
			std::atomic<uint32> overrideButtons{0};
			std::atomic<sint64> injectMouseDX{0}, injectMouseDY{0};
			std::atomic<uint32> cameraObject{0};
			std::atomic<uint32> lastHold{0};
			// virtual pointer for automated menu tests (bypasses focus)
			std::atomic<bool> overrideMouse{false};
			std::atomic<float> overrideMouseX{0}, overrideMouseY{0};
			std::atomic<bool> overrideLmb{false}, overrideRmb{false};
			std::atomic<sint32> injectWheel{0};
			// Magnesis: how fast the held object follows the view (degrees per second, and metres per second along its
			// circle, whichever is less)
			std::atomic<float> magnetFollowDeg{360.0f}, magnetFollowMetres{40.0f};
			std::atomic<uint64> overrideKeysAction{0}; // bit per Action, held as if pressed
			std::atomic<bool> lastPointerMode{false}, lastGameMenu{false};
			// motion tests: fixed tilt pose (degrees) instead of the mouse driven one
			std::atomic<bool> overrideTilt{false};
			std::atomic<float> overrideTiltPitch{0}, overrideTiltRoll{0};
		};
		Debug& GetDebug();
		// diagnostics: who reads the GamePad (return address) and how many samples it asks for
		void NoteVPADCaller(uint32 lr, uint32 length);
		// diagnostics: frames since the chase / aim camera hooks last ran
		std::string CameraAges();
		// dev: a client area position mapped to the game's picture, and the picture's place in the window
		std::string DebugPictureMap(float x, float y, float clientW, float clientH);
		std::string VPADCallers();
		// diagnostics: the motion values the game was given last (pose, attitude matrix, angles, rates)
		std::string MotionInfo();
	}
}

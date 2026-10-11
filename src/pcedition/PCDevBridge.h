#pragma once

// Zelda on PC - developer bridge (only with --pce-dev).
// Line based TCP protocol on 127.0.0.1:28770 used by automated test/measurement scripts.

namespace pce::DevBridge
{
	void Start();
	void Stop();
}

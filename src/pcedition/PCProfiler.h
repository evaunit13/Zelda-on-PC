#pragma once

// Zelda on PC - sampling profiler for the emulated CPU (dev tool, driven through the dev bridge).
//
// A sampler thread periodically suspends the emulated core threads (OSSched[core=N]) and the GPU command thread,
// reads their instruction pointer and attributes the sample to: a recompiled PPC basic block / function, an HLE
// function (coreinit, gx2, ...), the scheduler idle loop, a host wait, or other native code (module offset, resolved
// with the linker map). This tells where the frame time of the game goes.

#include <vector>
#include <string>
#include <atomic>

namespace pce::Profiler
{
	// per emulated core: HLE function index + 1 currently executing (0 = none) and the PPC return address of that call,
	// idle loop flag, OSThread currently loaded
	extern volatile uint32 gHle[4];
	extern volatile uint32 gHleLr[4];
	extern volatile uint32 gIdle[4];
	extern volatile uint32 gThread[4];

	inline void EnterHle(uint32 core, uint32 hleIndex, uint32 lr) { gHleLr[core & 3] = lr; gHle[core & 3] = hleIndex + 1; }
	inline void LeaveHle(uint32 core) { gHle[core & 3] = 0; }

	// recompiler bookkeeping: host code range -> PPC addresses (segments: host offset, PPC address)
	void RegisterFunction(const void* hostCode, size_t hostSize, uint32 ppcAddress, std::vector<std::pair<uint32, uint32>>&& segments);
	void UnregisterFunction(const void* hostCode);

	// runtime performance switches (dev bridge: perf ...)
	extern std::atomic<bool> gYieldFastPath; // OSYieldThread without a context switch when no other thread would run

	// keeps the emulated CPU cores and the GPU command thread on separate physical cores of one L3 cache group (Ryzen
	// CCX): no cross-CCX traffic between the threads that talk to each other all the time, no SMT sharing. Returns a
	// description of what was done.
	std::string PinEmulationThreads(bool enable);

	std::string Start(int hz);
	// stops sampling, writes the full report to reportPath and returns a short summary
	std::string Stop(const std::string& reportPath, int top);
	bool Running();
}

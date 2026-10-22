#pragma once

// Zelda on PC - AMD FidelityFX Super Resolution 3.1 frame generation (FidelityFX API, Vulkan).
//
// amd_fidelityfx_vk.dll (AMD FidelityFX SDK 1.1.4, prebuilt signed, MIT license) is loaded at runtime from the folder of
// the executable. The main window swapchain is replaced by the FidelityFX frame interpolation swapchain, which runs the
// interpolation on its own queues and paces the real and generated frames on its own threads. Per frame the renderer
// passes depth, motion vectors, camera and a HUD-less image (VulkanFSRInputs).

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

class VulkanFFX
{
public:
	struct QueueInfo
	{
		VkQueue queue = VK_NULL_HANDLE;
		uint32 family = 0;
	};

	struct FrameInfo
	{
		VkCommandBuffer cmd;
		// inputs (render resolution)
		VkImage depth; // R32_SFLOAT, GENERAL layout, [0..1] near..far
		VkImage motionVectors; // RG16F, GENERAL layout, previous uv - current uv
		uint32 renderWidth, renderHeight;
		// HUD-less image (display resolution, SHADER_READ_ONLY_OPTIMAL)
		VkImage hudless;
		VkFormat hudlessFormat;
		// camera
		float cameraNear, cameraFar, fovY; // radians
		float position[3], up[3], right[3], forward[3];
		float frameTimeMs;
		bool reset;
		uint32 debugFlags; // FfxApiDispatchFramegenerationFlags (debug views)
	};

	static VulkanFFX& Get();

	// loads the library once; false if missing
	bool Load();
	bool IsLoaded() const { return m_loaded; }

	void SetQueues(const QueueInfo& game, const QueueInfo& present, const QueueInfo& acquire, const QueueInfo& asyncCompute);
	bool HasQueues() const { return m_present.queue && m_acquire.queue && m_compute.queue; }
	QueueInfo PresentQueue() const { return m_present; }

	// swapchain: replaces *swapchain (created with createInfo) with the frame interpolation swapchain
	bool CreateSwapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSwapchainKHR* swapchain, const VkSwapchainCreateInfoKHR& createInfo);
	void DestroySwapchain(); // also destroys the frame generation context
	bool HasSwapchain() const { return m_swapchainContext != nullptr; }
	VkSwapchainKHR Swapchain() const { return m_swapchain; }

	VkResult GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32* count, VkImage* images);
	VkResult AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64 timeout, VkSemaphore semaphore, VkFence fence, uint32* index);
	VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR* info);
	uint64 PresentCount() const;

	// per frame, before the present of the frame: configure + record the prepare dispatch into info.cmd
	bool PrepareFrame(VkPhysicalDevice physicalDevice, VkDevice device, VkExtent2D displaySize, VkFormat backbufferFormat, const FrameInfo& info);
	// present without frame generation (paused, menus without game frames...)
	void DisableGeneration();

private:
	bool EnsureFrameGenContext(VkPhysicalDevice physicalDevice, VkDevice device, VkExtent2D displaySize, VkExtent2D renderSize, VkFormat backbufferFormat, VkFormat hudlessFormat);
	void DestroyFrameGenContext();

	bool m_loaded = false;
	bool m_loadTried = false;
	void* m_module = nullptr;
	void* m_fnCreate = nullptr;
	void* m_fnDestroy = nullptr;
	void* m_fnConfigure = nullptr;
	void* m_fnQuery = nullptr;
	void* m_fnDispatch = nullptr;

	QueueInfo m_game, m_present, m_acquire, m_compute;

	void* m_swapchainContext = nullptr;
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	void* m_fnGetImages = nullptr;
	void* m_fnAcquire = nullptr;
	void* m_fnPresent = nullptr;
	void* m_fnPresentCount = nullptr;

	void* m_fgContext = nullptr;
	VkExtent2D m_fgDisplay{}, m_fgRender{};
	VkFormat m_fgBackbuffer = VK_FORMAT_UNDEFINED, m_fgHudless = VK_FORMAT_UNDEFINED;
	uint64 m_frameID = 0;
	bool m_resetPending = true;
};

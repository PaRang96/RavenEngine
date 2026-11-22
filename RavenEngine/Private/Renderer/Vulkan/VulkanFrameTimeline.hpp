#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

namespace Raven
{
	// One timeline semaphore counts submitted frames, and released or replaced resources wait
	// in one queue until the GPU passes every frame that could use them (docs/RendererDesign.md, R3).
	// It outlives swapchain recreation, so its values only grow.
	class VulkanFrameTimeline
	{
	public:
		explicit VulkanFrameTimeline(const VulkanDevice& device);
		// The owner waits for device idle first; queued resources are then dropped.
		~VulkanFrameTimeline();

		VkSemaphore Get() const noexcept { return m_Semaphore; }
		// The value the next frame submission signals.
		std::uint64_t GetNextValue() const noexcept { return m_Submitted + 1; }
		// Call after a submission that signals GetNextValue() was queued.
		void MarkSubmitted() noexcept { ++m_Submitted; }
		// Blocks until the GPU reaches value. A value already reached returns without a driver wait.
		void Wait(std::uint64_t value);

		// Keeps a released or replaced resource until every frame submitted so far completes.
		void Retire(std::shared_ptr<const void> resource) noexcept;
		// Drops queued resources whose frames have completed.
		void Collect() noexcept;
		std::size_t GetRetiredCount() const noexcept { return m_Retired.size(); }

		VulkanFrameTimeline(const VulkanFrameTimeline&) = delete;
		VulkanFrameTimeline& operator=(const VulkanFrameTimeline&) = delete;

	private:
		bool Reached(std::uint64_t value) noexcept;

		struct Retired
		{
			std::uint64_t Value = 0;
			std::shared_ptr<const void> Resource;
		};

		VkDevice m_Device = VK_NULL_HANDLE;
		VkSemaphore m_Semaphore = VK_NULL_HANDLE;
		std::uint64_t m_Submitted = 0;
		// Last value read back from the GPU; never ahead of the semaphore.
		std::uint64_t m_Completed = 0;
		// Values never decrease, so the front always completes first.
		std::deque<Retired> m_Retired;
	};
}

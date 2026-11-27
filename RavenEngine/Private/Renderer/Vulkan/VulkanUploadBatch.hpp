#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{
	class VulkanFrameTimeline;
	struct VulkanPhysicalDevice;

	// The per-frame upload batch (docs/RenderingContentDesign.md, B0 contract 5): texture contents and
	// mesh pool ranges created between frames are recorded into one command buffer, with no wait, and
	// submitted on the graphics queue just before the next frame. It signals the frame timeline and ends
	// in a transfer-to-shader and vertex-input barrier, so the frame that first draws with them finds
	// them filled.
	class VulkanUploadBatch
	{
	public:
		VulkanUploadBatch(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice, VulkanFrameTimeline& timeline);
		~VulkanUploadBatch();

		// The open command buffer, begun on first use since the last submission.
		VkCommandBuffer Record();
		// Keeps a resource the recorded commands use until the GPU has finished the batch.
		void Keep(std::shared_ptr<const void> resource);
		// Submits what was recorded, if anything; the frame submission follows on the same queue.
		void Submit();
		bool IsOpen() const noexcept { return m_Open != nullptr; }
		// GPU time of the batches that completed since the last call, from timestamps around each batch
		// (texture copies, mip generation and mesh copies; docs/RenderingContentDesign.md, measurement 6).
		// Never waits; zero without timestamp support.
		std::int64_t TakeGpuNanoseconds() noexcept;

		VulkanUploadBatch(const VulkanUploadBatch&) = delete;
		VulkanUploadBatch& operator=(const VulkanUploadBatch&) = delete;

	private:
		struct Slot
		{
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			// The timeline value of its last submission; reused once the GPU reaches it.
			std::uint64_t Submitted = 0;
			// Its timestamps are written and not read yet.
			bool Timed = false;
		};

		void ReadTimestamps(Slot& slot) noexcept;

		const VulkanDevice& m_DeviceOwner;
		VulkanFrameTimeline& m_Timeline;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_Queue = VK_NULL_HANDLE;
		VkCommandPool m_Pool = VK_NULL_HANDLE;
		// A few slots, so a batch records while earlier ones are still on the GPU.
		std::array<Slot, 3> m_Slots{};
		std::size_t m_Next = 0;
		Slot* m_Open = nullptr;
		std::vector<std::shared_ptr<const void>> m_Kept;
		// Two timestamps per slot, when the graphics queue supports them.
		VkQueryPool m_Queries = VK_NULL_HANDLE;
		std::uint64_t m_TimestampMask = 0;
		double m_TimestampPeriod = 0.0;
		std::int64_t m_GpuNanoseconds = 0;
	};
}

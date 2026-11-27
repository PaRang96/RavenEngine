#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{
	class VulkanFrameTimeline;

	// The per-frame upload batch (docs/RenderingContentDesign.md, B0 contract 5): texture contents and
	// mesh pool ranges created between frames are recorded into one command buffer, with no wait, and
	// submitted on the graphics queue just before the next frame. It signals the frame timeline and ends
	// in a transfer-to-shader and vertex-input barrier, so the frame that first draws with them finds
	// them filled.
	class VulkanUploadBatch
	{
	public:
		VulkanUploadBatch(const VulkanDevice& device, std::uint32_t graphicsFamily, VulkanFrameTimeline& timeline);
		~VulkanUploadBatch();

		// The open command buffer, begun on first use since the last submission.
		VkCommandBuffer Record();
		// Keeps a resource the recorded commands use until the GPU has finished the batch.
		void Keep(std::shared_ptr<const void> resource);
		// Submits what was recorded, if anything; the frame submission follows on the same queue.
		void Submit();
		bool IsOpen() const noexcept { return m_Open != nullptr; }

		VulkanUploadBatch(const VulkanUploadBatch&) = delete;
		VulkanUploadBatch& operator=(const VulkanUploadBatch&) = delete;

	private:
		struct Slot
		{
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			// The timeline value of its last submission; reused once the GPU reaches it.
			std::uint64_t Submitted = 0;
		};

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
	};
}

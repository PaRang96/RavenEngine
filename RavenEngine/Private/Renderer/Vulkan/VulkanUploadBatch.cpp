#include "Renderer/Vulkan/VulkanUploadBatch.hpp"

#include "Renderer/Vulkan/VulkanFrameTimeline.hpp"

#include <utility>

namespace Raven
{
	VulkanUploadBatch::VulkanUploadBatch(const VulkanDevice& device, std::uint32_t graphicsFamily,
		VulkanFrameTimeline& timeline)
		: m_DeviceOwner(device), m_Timeline(timeline), m_Device(device.Get()), m_Queue(device.GetGraphicsQueue())
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = graphicsFamily;
		CheckVulkanResult(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_Pool), "vkCreateCommandPool (upload batch)");
		std::array<VkCommandBuffer, std::tuple_size_v<decltype(m_Slots)>> buffers{};
		VkCommandBufferAllocateInfo allocation{};
		allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocation.commandPool = m_Pool;
		allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocation.commandBufferCount = static_cast<std::uint32_t>(buffers.size());
		const VkResult result = vkAllocateCommandBuffers(m_Device, &allocation, buffers.data());
		if (result != VK_SUCCESS)
		{
			vkDestroyCommandPool(m_Device, m_Pool, nullptr);
			CheckVulkanResult(result, "vkAllocateCommandBuffers (upload batch)");
		}
		for (std::size_t index = 0; index < buffers.size(); ++index)
			m_Slots[index].Commands = buffers[index];
	}

	VulkanUploadBatch::~VulkanUploadBatch()
	{
		// The context waits for the device first; an unsubmitted batch is simply dropped.
		vkDestroyCommandPool(m_Device, m_Pool, nullptr);
	}

	VkCommandBuffer VulkanUploadBatch::Record()
	{
		if (m_Open)
			return m_Open->Commands;
		auto& slot = m_Slots[m_Next];
		// The slot's previous batch has to finish before its commands are reset; with three slots
		// and one batch per frame this is already true in practice.
		m_Timeline.Wait(slot.Submitted);
		CheckVulkanResult(vkResetCommandBuffer(slot.Commands, 0), "vkResetCommandBuffer (upload batch)");
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CheckVulkanResult(vkBeginCommandBuffer(slot.Commands, &begin), "vkBeginCommandBuffer (upload batch)");
		m_Open = &slot;
		m_Next = (m_Next + 1) % m_Slots.size();
		return slot.Commands;
	}

	void VulkanUploadBatch::Keep(std::shared_ptr<const void> resource)
	{
		m_Kept.push_back(std::move(resource));
	}

	void VulkanUploadBatch::Submit()
	{
		if (!m_Open)
			return;
		auto& slot = *m_Open;
		m_Open = nullptr;
		// Everything the batch wrote is visible to the vertex input and the vertex and fragment shaders of
		// later frames: texture contents and mesh pool ranges (FD-22).
		VkMemoryBarrier2KHR barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT_KHR;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR |
			VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR |
			VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT_KHR | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT_KHR |
			VK_ACCESS_2_INDEX_READ_BIT_KHR | VK_ACCESS_2_TRANSFER_READ_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
		VkDependencyInfoKHR dependency{};
		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers = &barrier;
		m_DeviceOwner.GetCommands().PipelineBarrier2(slot.Commands, &dependency);
		CheckVulkanResult(vkEndCommandBuffer(slot.Commands), "vkEndCommandBuffer (upload batch)");

		const std::uint64_t value = m_Timeline.GetNextValue();
		VkSemaphoreSubmitInfoKHR signal{};
		signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
		signal.semaphore = m_Timeline.Get();
		signal.value = value;
		signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
		VkCommandBufferSubmitInfoKHR commandInfo{};
		commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
		commandInfo.commandBuffer = slot.Commands;
		VkSubmitInfo2KHR submission{};
		submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR;
		submission.commandBufferInfoCount = 1;
		submission.pCommandBufferInfos = &commandInfo;
		submission.signalSemaphoreInfoCount = 1;
		submission.pSignalSemaphoreInfos = &signal;
		CheckVulkanResult(m_DeviceOwner.GetCommands().QueueSubmit2(m_Queue, 1, &submission, VK_NULL_HANDLE),
			"vkQueueSubmit2KHR (upload batch)");
		m_Timeline.MarkSubmitted();
		slot.Submitted = value;
		// Staging buffers and textures created into the batch live until the GPU passes it (R3).
		for (auto& resource : m_Kept)
			m_Timeline.Retire(std::move(resource));
		m_Kept.clear();
	}
}

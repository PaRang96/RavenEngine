#include "Renderer/Vulkan/VulkanFrameTimeline.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace Raven
{
	VulkanFrameTimeline::VulkanFrameTimeline(const VulkanDevice& device)
		: m_Device(device.Get())
	{
		VkSemaphoreTypeCreateInfo type{};
		type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		type.initialValue = 0;
		VkSemaphoreCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		info.pNext = &type;
		const VkResult result = vkCreateSemaphore(m_Device, &info, nullptr, &m_Semaphore);
		if (result != VK_SUCCESS)
		{
			m_Semaphore = VK_NULL_HANDLE;
			throw std::runtime_error("vkCreateSemaphore (frame timeline) failed: " + std::to_string(result));
		}
	}

	VulkanFrameTimeline::~VulkanFrameTimeline()
	{
		m_Retired.clear();
		if (m_Semaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(m_Device, m_Semaphore, nullptr);
	}

	bool VulkanFrameTimeline::Reached(std::uint64_t value) noexcept
	{
		if (value <= m_Completed)
			return true;
		std::uint64_t completed = 0;
		if (vkGetSemaphoreCounterValue(m_Device, m_Semaphore, &completed) != VK_SUCCESS)
			return false;
		m_Completed = completed;
		return value <= m_Completed;
	}

	void VulkanFrameTimeline::Wait(std::uint64_t value)
	{
		// The counter read is far cheaper than vkWaitSemaphores when the frame has already completed.
		if (Reached(value))
			return;
		VkSemaphoreWaitInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		info.semaphoreCount = 1;
		info.pSemaphores = &m_Semaphore;
		info.pValues = &value;
		const VkResult result = vkWaitSemaphores(m_Device, &info, std::numeric_limits<std::uint64_t>::max());
		CheckVulkanResult(result, "vkWaitSemaphores (frame timeline)");
		m_Completed = value;
	}

	void VulkanFrameTimeline::Retire(std::shared_ptr<const void> resource) noexcept
	{
		if (!resource)
			return;
		try
		{
			// Copy, so the resource survives a failed insertion.
			m_Retired.push_back(Retired{ m_Submitted, resource });
		}
		catch (...)
		{
			// Without queue storage, finish all GPU work before the resource is dropped.
			vkDeviceWaitIdle(m_Device);
		}
	}

	void VulkanFrameTimeline::Collect() noexcept
	{
		while (!m_Retired.empty() && Reached(m_Retired.front().Value))
			m_Retired.pop_front();
	}
}

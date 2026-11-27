#include "Renderer/Vulkan/VulkanFrameData.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace Raven
{
	VulkanFrameLayout::VulkanFrameLayout(const VulkanDevice& device)
		: m_Device(device.Get())
	{
		const VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		VkDescriptorSetLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.bindingCount = 1;
		info.pBindings = &binding;
		const VkResult result = vkCreateDescriptorSetLayout(m_Device, &info, nullptr, &m_Layout);
		if (result != VK_SUCCESS)
		{
			m_Layout = VK_NULL_HANDLE;
			throw std::runtime_error("vkCreateDescriptorSetLayout (frame) failed: " + std::to_string(result));
		}
	}

	VulkanFrameLayout::~VulkanFrameLayout()
	{
		if (m_Layout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
	}

	bool VulkanUploadRing::Begin(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
		VulkanFrameTimeline& timeline, VkDeviceSize required)
	{
		m_Offset = 0;
		if (m_Buffer && m_Buffer->GetSize() >= required)
			return false;
		// Grow geometrically and keep the size across frames, so steady scenes never reallocate.
		constexpr VkDeviceSize maxBytes = std::numeric_limits<VkDeviceSize>::max();
		VkDeviceSize capacity = m_Buffer ? m_Buffer->GetSize() : 64 * 1024;
		while (capacity < required)
			capacity = capacity <= maxBytes / 2 ? capacity * 2 : required;
		auto replacement = std::make_shared<VulkanBuffer>(device, physicalDevice, capacity, Usage,
			VulkanBuffer::Mapping::Persistent);
		timeline.Retire(std::move(m_Buffer));
		m_Buffer = std::move(replacement);
		return true;
	}

	VkDeviceSize VulkanUploadRing::Push(std::span<const std::byte> bytes)
	{
		const VkDeviceSize offset = Align(m_Offset);
		if (!m_Buffer || offset > m_Buffer->GetSize() || bytes.size() > m_Buffer->GetSize() - offset)
			throw std::length_error("Upload ring allocation exceeds the frame's reserved size");
		if (!bytes.empty())
			std::memcpy(m_Buffer->GetMapped() + offset, bytes.data(), bytes.size());
		m_Offset = offset + bytes.size();
		return offset;
	}
}

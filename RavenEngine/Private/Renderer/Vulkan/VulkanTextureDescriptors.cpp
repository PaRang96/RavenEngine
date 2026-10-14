#include "Renderer/Vulkan/VulkanTextureDescriptors.hpp"

#include "Renderer/Vulkan/VulkanTexture.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		void CheckDescriptorResult(VkResult result, const char* operation)
		{
			if (result != VK_SUCCESS)
				throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
		}
	}

	VulkanTextureDescriptors::VulkanTextureDescriptors(const VulkanDevice& device)
		: m_Device(device.Get())
	{
		if (m_Device == VK_NULL_HANDLE)
			throw std::invalid_argument("Texture descriptors require a device");

		const std::array<VkDescriptorSetLayoutBinding, 2> bindings
		{
			VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_SAMPLER,
				1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
		};
		VkDescriptorSetLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.bindingCount = static_cast<std::uint32_t>(bindings.size());
		info.pBindings = bindings.data();
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		CheckDescriptorResult(vkCreateDescriptorSetLayout(m_Device, &info, nullptr, &layout),
			"vkCreateDescriptorSetLayout (texture)");
		m_Layout = layout;
	}

	void VulkanTextureDescriptors::AddPage()
	{
		// Reserve before creating the native owner so a vector allocation cannot leak it.
		if (m_Pages.size() == m_Pages.capacity())
			m_Pages.reserve(m_Pages.empty() ? 1 : m_Pages.size() * 2);
		const std::array<VkDescriptorPoolSize, 2> sizes
		{
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SetsPerPage },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, SetsPerPage }
		};
		VkDescriptorPoolCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		info.maxSets = SetsPerPage;
		info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
		info.pPoolSizes = sizes.data();
		VkDescriptorPool pool = VK_NULL_HANDLE;
		CheckDescriptorResult(vkCreateDescriptorPool(m_Device, &info, nullptr, &pool),
			"vkCreateDescriptorPool (texture)");
		m_Pages.push_back({ pool, 0 });
	}

	VkDescriptorSet VulkanTextureDescriptors::CreateBinding(const VulkanTexture& texture)
	{
		if (texture.GetDevice() != m_Device || texture.GetView() == VK_NULL_HANDLE ||
			texture.GetSampler() == VK_NULL_HANDLE)
			throw std::invalid_argument("Texture descriptors require a texture from the same device");
		if (m_Pages.empty() || m_Pages.back().Used == SetsPerPage)
			AddPage();

		VkDescriptorSetAllocateInfo allocation{};
		allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocation.descriptorPool = m_Pages.back().Pool;
		allocation.descriptorSetCount = 1;
		allocation.pSetLayouts = &m_Layout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		CheckDescriptorResult(vkAllocateDescriptorSets(m_Device, &allocation, &set),
			"vkAllocateDescriptorSets (texture)");
		++m_Pages.back().Used;

		const VkDescriptorImageInfo image{ VK_NULL_HANDLE, texture.GetView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		const VkDescriptorImageInfo sampler{ texture.GetSampler(), VK_NULL_HANDLE,
			VK_IMAGE_LAYOUT_UNDEFINED };
		std::array<VkWriteDescriptorSet, 2> writes{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = set;
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		writes[0].pImageInfo = &image;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = set;
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		writes[1].pImageInfo = &sampler;
		vkUpdateDescriptorSets(m_Device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
		return set;
	}

	VulkanTextureDescriptors::~VulkanTextureDescriptors()
	{
		Destroy();
	}

	void VulkanTextureDescriptors::Destroy() noexcept
	{
		for (const auto& page : m_Pages)
			vkDestroyDescriptorPool(m_Device, page.Pool, nullptr);
		m_Pages.clear();
		if (m_Layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
			m_Layout = VK_NULL_HANDLE;
		}
	}
}

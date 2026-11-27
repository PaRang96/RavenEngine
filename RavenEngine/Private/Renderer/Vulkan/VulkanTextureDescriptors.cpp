#include "Renderer/Vulkan/VulkanTextureDescriptors.hpp"

#include "Raven/Core/Log.hpp"

#include "Renderer/Vulkan/VulkanTexture.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <stdexcept>
#include <utility>
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

		try
		{
			// Elements change only after the frames that used them retire, so updates never
			// touch a descriptor a pending frame reads (update-after-bind, unused while pending).
			const std::array<VkDescriptorSetLayoutBinding, 2> bindless
			{
				VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
					BindlessCapacity, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_SAMPLER,
					1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
			};
			const std::array<VkDescriptorBindingFlags, 2> flags
			{
				VkDescriptorBindingFlags{ VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
					VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT },
				VkDescriptorBindingFlags{ 0 }
			};
			VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{};
			flagInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
			flagInfo.bindingCount = static_cast<std::uint32_t>(flags.size());
			flagInfo.pBindingFlags = flags.data();
			VkDescriptorSetLayoutCreateInfo bindlessInfo{};
			bindlessInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			bindlessInfo.pNext = &flagInfo;
			bindlessInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
			bindlessInfo.bindingCount = static_cast<std::uint32_t>(bindless.size());
			bindlessInfo.pBindings = bindless.data();
			CheckDescriptorResult(vkCreateDescriptorSetLayout(m_Device, &bindlessInfo, nullptr, &m_BindlessLayout),
				"vkCreateDescriptorSetLayout (bindless textures)");

			const std::array<VkDescriptorPoolSize, 2> sizes
			{
				VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, BindlessCapacity },
				VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1 }
			};
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
			poolInfo.maxSets = 1;
			poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
			poolInfo.pPoolSizes = sizes.data();
			CheckDescriptorResult(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_BindlessPool),
				"vkCreateDescriptorPool (bindless textures)");
			VkDescriptorSetAllocateInfo allocation{};
			allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocation.descriptorPool = m_BindlessPool;
			allocation.descriptorSetCount = 1;
			allocation.pSetLayouts = &m_BindlessLayout;
			CheckDescriptorResult(vkAllocateDescriptorSets(m_Device, &allocation, &m_BindlessSet),
				"vkAllocateDescriptorSets (bindless textures)");

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			// The one sampler every texture shares (FD-21): linear, blending between mip levels, so
			// textures with a mip chain stay smooth in the distance and single-level ones sample as
			// before. A repeat sampler and the per-material choice come with B4's fragment ABI.
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
			CheckDescriptorResult(vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler),
				"vkCreateSampler (bindless textures)");
			const VkDescriptorImageInfo sampler{ m_Sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = m_BindlessSet;
			write.dstBinding = 1;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			write.pImageInfo = &sampler;
			vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
		}
		catch (...)
		{
			Destroy();
			throw;
		}
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
		info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		info.maxSets = SetsPerPage;
		info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
		info.pPoolSizes = sizes.data();
		VkDescriptorPool pool = VK_NULL_HANDLE;
		CheckDescriptorResult(vkCreateDescriptorPool(m_Device, &info, nullptr, &pool),
			"vkCreateDescriptorPool (texture)");
		m_Pages.push_back({ pool, 0 });
	}

	VkDescriptorSet VulkanTextureDescriptors::CreateBinding(VulkanTexture& texture, TextureUse use)
	{
		if (texture.GetDevice() != m_Device || texture.GetView() == VK_NULL_HANDLE)
			throw std::invalid_argument("Texture descriptors require a texture from the same device");
		auto page = std::find_if(m_Pages.begin(), m_Pages.end(), [](const Page& value)
		{
			return value.Used < SetsPerPage;
		});
		if (page == m_Pages.end())
		{
			AddPage();
			page = std::prev(m_Pages.end());
		}

		VkDescriptorSetAllocateInfo allocation{};
		allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocation.descriptorPool = page->Pool;
		allocation.descriptorSetCount = 1;
		allocation.pSetLayouts = &m_Layout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		CheckDescriptorResult(vkAllocateDescriptorSets(m_Device, &allocation, &set),
			"vkAllocateDescriptorSets (texture)");
		try
		{
			m_Bindings.emplace(set, static_cast<std::size_t>(page - m_Pages.begin()));
		}
		catch (...)
		{
			vkFreeDescriptorSets(m_Device, page->Pool, 1, &set);
			throw;
		}
		++page->Used;
		WriteSet(set, texture);
		// The first texture is the context's white, element 0.
		if (m_WhiteView == VK_NULL_HANDLE)
		{
			m_WhiteView = texture.GetView();
			texture.SetBindlessIndex(0);
			WriteElement(0, texture);
			return set;
		}
		auto& elements = m_Classes[use == TextureUse::Engine ? 0 : 1];
		std::uint32_t element = 0;
		if (!elements.Free.empty())
		{
			element = elements.Free.back();
			elements.Free.pop_back();
		}
		else if (elements.Next < elements.End)
			element = elements.Next++;
		if (element == 0)
		{
			// Full: the texture samples white, one line per fill (FD-06, a data error, not fatal).
			if (!std::exchange(elements.ReportedFull, true))
				Log(LogLevel::Warning, std::format("The bindless {} textures are full ({} elements); further textures draw "
					"white until some are released", use == TextureUse::Engine ? "engine" : "content", elements.End - elements.First));
			texture.SetBindlessIndex(0);
			return set;
		}
		++elements.Used;
		texture.SetBindlessIndex(element);
		WriteElement(element, texture);
		return set;
	}

	void VulkanTextureDescriptors::ReleaseBinding(const VulkanTexture& texture) noexcept
	{
		const auto element = texture.GetBindlessIndex();
		for (auto& elements : m_Classes)
			if (element >= elements.First && element < elements.End)
			{
				// Freed elements sample white until reused, so a stale index never shows another
				// texture (AA2-RND-05 step 1).
				if (m_WhiteView != VK_NULL_HANDLE)
				{
					const VkDescriptorImageInfo image{ VK_NULL_HANDLE, m_WhiteView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
					VkWriteDescriptorSet write{};
					write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					write.dstSet = m_BindlessSet;
					write.dstBinding = 0;
					write.dstArrayElement = element;
					write.descriptorCount = 1;
					write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					write.pImageInfo = &image;
					vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
				}
				try
				{
					elements.Free.push_back(element);
					--elements.Used;
					elements.ReportedFull = false;
				}
				catch (...)
				{
					// The element stays unused until shutdown.
				}
			}
		auto binding = texture.GetDescriptorSet();
		const auto found = m_Bindings.find(binding);
		if (found == m_Bindings.end())
			return;
		auto& page = m_Pages[found->second];
		if (vkFreeDescriptorSets(m_Device, page.Pool, 1, &binding) == VK_SUCCESS)
		{
			--page.Used;
			m_Bindings.erase(found);
		}
	}

	void VulkanTextureDescriptors::WriteBinding(const VulkanTexture& target, const VulkanTexture& contents) const noexcept
	{
		WriteSet(target.GetDescriptorSet(), contents);
		// A texture created while its class was full shares element 0 and must not overwrite white.
		if (target.GetBindlessIndex() != 0 || target.GetView() == m_WhiteView)
			WriteElement(target.GetBindlessIndex(), contents);
	}

	void VulkanTextureDescriptors::WriteElement(std::uint32_t element, const VulkanTexture& contents) const noexcept
	{
		const VkDescriptorImageInfo image{ VK_NULL_HANDLE, contents.GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_BindlessSet;
		write.dstBinding = 0;
		write.dstArrayElement = element;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo = &image;
		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
	}

	void VulkanTextureDescriptors::WriteSet(VkDescriptorSet set, const VulkanTexture& texture) const noexcept
	{
		const VkDescriptorImageInfo image{ VK_NULL_HANDLE, texture.GetView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		// Per-texture sets name the shared sampler too; textures no longer own one.
		const VkDescriptorImageInfo sampler{ m_Sampler, VK_NULL_HANDLE,
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
		if (m_Sampler != VK_NULL_HANDLE)
			vkDestroySampler(m_Device, m_Sampler, nullptr);
		m_Sampler = VK_NULL_HANDLE;
		if (m_BindlessPool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(m_Device, m_BindlessPool, nullptr);
		m_BindlessPool = VK_NULL_HANDLE;
		if (m_BindlessLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_Device, m_BindlessLayout, nullptr);
		m_BindlessLayout = VK_NULL_HANDLE;
		if (m_Layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
			m_Layout = VK_NULL_HANDLE;
		}
	}
}

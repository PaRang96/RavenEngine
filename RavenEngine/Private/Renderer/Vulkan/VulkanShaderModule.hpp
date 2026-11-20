#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Raven
{
	class VulkanShaderModule
	{
	public:
		VulkanShaderModule(const VulkanDevice& device, const std::filesystem::path& path);
		VulkanShaderModule(const VulkanDevice& device, std::span<const std::uint32_t> words);
		static std::vector<std::uint32_t> ReadWords(const std::filesystem::path& path);
		~VulkanShaderModule();

		VkShaderModule Get() const { return m_Module; }

		VulkanShaderModule(const VulkanShaderModule&) = delete;
		VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		VkShaderModule m_Module = VK_NULL_HANDLE;
	};
}

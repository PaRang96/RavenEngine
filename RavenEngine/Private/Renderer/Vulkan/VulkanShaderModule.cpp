#include "Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace Raven
{
	VulkanShaderModule::VulkanShaderModule(const VulkanDevice& device,
		const std::filesystem::path& path)
		: m_Device(device.Get())
	{
		if (m_Device == VK_NULL_HANDLE)
			throw std::runtime_error("A logical device is required for shader loading");

		const auto bytes = Files::ReadBinary(path);
		if (bytes.size() < 5 * sizeof(std::uint32_t) ||
			bytes.size() % sizeof(std::uint32_t) != 0)
			throw std::runtime_error("Invalid SPIR-V size: " + PathToUtf8(path));

		// Word storage satisfies Vulkan's alignment requirement for pCode.
		std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
		std::memcpy(words.data(), bytes.data(), bytes.size());
		if (words.front() != 0x07230203)
			throw std::runtime_error("Invalid SPIR-V magic: " + PathToUtf8(path));

		VkShaderModuleCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		info.codeSize = bytes.size();
		info.pCode = words.data();
		VkShaderModule module = VK_NULL_HANDLE;
		const VkResult result = vkCreateShaderModule(m_Device, &info, nullptr, &module);
		if (result != VK_SUCCESS)
			throw std::runtime_error("vkCreateShaderModule failed for " + PathToUtf8(path) +
				": " + std::to_string(result));
		m_Module = module;
	}

	VulkanShaderModule::~VulkanShaderModule()
	{
		if (m_Module != VK_NULL_HANDLE)
			vkDestroyShaderModule(m_Device, m_Module, nullptr);
	}
}

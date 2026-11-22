#include "Renderer/Vulkan/VulkanInstance.hpp"
#include <stdexcept>
#include <string>
#include <cstring>
#include <algorithm>
#include <cstdint>

namespace Raven
{
	namespace
	{
		std::vector<VkExtensionProperties> ReadInstanceExtensions(const char* layerName)
		{
			std::vector<VkExtensionProperties> properties;
			VkResult result = VK_SUCCESS;

			do
			{
				std::uint32_t count = 0;
				result = vkEnumerateInstanceExtensionProperties(
					layerName, &count, nullptr);

				if (result != VK_SUCCESS)
				{
					throw std::runtime_error(
						"Could not enumerate Vulkan instance extensions: " +
						std::to_string(result));
				}

				properties.resize(count);
				if (count == 0)
					return properties;

				result = vkEnumerateInstanceExtensionProperties(
					layerName, &count, properties.data());

				if (result != VK_SUCCESS && result != VK_INCOMPLETE)
				{
					throw std::runtime_error(
						"Could not read Vulkan instance extensions: " +
						std::to_string(result));
				}

				properties.resize(count);
			}
			while (result == VK_INCOMPLETE);

			return properties;
		}
	}

	VulkanInstance::VulkanInstance(const std::vector<const char*> extensions)
	{
		#ifdef RAVEN_ENABLE_VALIDATION
		const char* validationLayer = "VK_LAYER_KHRONOS_validation";

		std::uint32_t layerCount = 0;
		if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr))
			throw std::runtime_error("Could not enumerate Vulkan layers");

		std::vector<VkLayerProperties> layers(layerCount);
		if (vkEnumerateInstanceLayerProperties(&layerCount, layers.data()) != VK_SUCCESS)
			throw std::runtime_error("Could not read Vulkan layers");

		bool found = false;
		for (const auto& layer : layers)
		{
			if (std::strcmp(layer.layerName, validationLayer) == 0)
				found = true;
		}

		if (!found)
			throw std::runtime_error("Vulkan validation layer is unavailable");
		#endif // RAVEN_ENABLE_VALIDATION

		// docs/RendererDesign.md, R1: the loader must expose Vulkan 1.2.
		std::uint32_t loaderVersion = VK_API_VERSION_1_0;
		if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS || loaderVersion < VK_API_VERSION_1_2)
			throw std::runtime_error("The installed Vulkan runtime supports Vulkan " +
				std::to_string(VK_API_VERSION_MAJOR(loaderVersion)) + "." +
				std::to_string(VK_API_VERSION_MINOR(loaderVersion)) +
				"; RavenEngine requires Vulkan 1.2. Update the GPU driver or Vulkan runtime.");

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "RavenEngine";
		appInfo.pEngineName = "RavenEngine";
		appInfo.apiVersion = VK_API_VERSION_1_2;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		auto enabledExtensions = extensions;

		// MoltenVK devices are exposed by the loader only with this flag.
		for (const char* extension : enabledExtensions)
		{
			if (std::strcmp(extension, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
				createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}

		#ifdef RAVEN_ENABLE_VALIDATION
		enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		createInfo.enabledLayerCount = 1;
		createInfo.ppEnabledLayerNames = &validationLayer;
		#endif //RAVEN_ENABLE_VALIDATION

		auto availableExtensions = ReadInstanceExtensions(nullptr);

		#ifdef RAVEN_ENABLE_VALIDATION
		const auto layerExtensions = ReadInstanceExtensions(validationLayer);
		availableExtensions.insert(
			availableExtensions.end(),
			layerExtensions.begin(),
			layerExtensions.end());
		#endif

		for (const char* required : enabledExtensions)
		{
			const bool found = std::any_of(
				availableExtensions.begin(),
				availableExtensions.end(),
				[required](const VkExtensionProperties& extension)
				{
					return std::strcmp(extension.extensionName, required) == 0;
				});

			if (!found)
			{
				throw std::runtime_error(
					std::string("Required Vulkan instance extension unavailable: ") +
					required);
			}
		}

		createInfo.enabledExtensionCount =
			static_cast<std::uint32_t>(enabledExtensions.size());
		createInfo.ppEnabledExtensionNames = enabledExtensions.data();

		const VkResult result =
			vkCreateInstance(&createInfo, nullptr, &m_Instance);
		if (result != VK_SUCCESS)
			throw std::runtime_error("vkCreateInstance failed: " +
				std::to_string(result));
	}

	VulkanInstance::~VulkanInstance()
	{
		if (m_Instance != VK_NULL_HANDLE)
			vkDestroyInstance(m_Instance, nullptr);
	}
}

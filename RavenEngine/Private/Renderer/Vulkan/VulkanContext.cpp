#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include "Renderer/Vulkan/VulkanInstance.hpp"
#include "Renderer/Vulkan/VulkanDebugMessenger.hpp"
#include "Renderer/Vulkan/VulkanSurface.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"
#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Renderer/Vulkan/VulkanTexturedMesh.hpp"
#include "Renderer/Vulkan/VulkanTexture.hpp"
#include "Renderer/Vulkan/VulkanTextureDescriptors.hpp"
#include "Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Renderer/Vulkan/VulkanClearPass.hpp"
#include "Renderer/Vulkan/VulkanDrawQueue.hpp"

#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Window.hpp"

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Raven
{
	struct VulkanContext::Impl
	{
		explicit Impl(const Window& window);
		~Impl();

		void PrepareSwapchain();
		void ResetSwapchain();
		const VulkanMesh& CreateMesh(std::span<const ColorVertex> vertices,
			std::span<const std::uint16_t> indices);
		const VulkanTexturedMesh& CreateTexturedMesh(std::span<const TexturedVertex> vertices,
			std::span<const std::uint16_t> indices);
		const VulkanTexture& CreateTexture(const TextureDesc& description);
		template<typename Camera>
		void DrawFrame(const Camera& camera, std::span<const VulkanDraw> draws,
			std::span<const VulkanTexturedDraw> texturedDraws,
			std::span<const VulkanTransparentDraw> transparentDraws);

		const Window& m_Window;
		const std::filesystem::path m_ShaderDirectory;

		// Owners are destroyed in reverse declaration order.
		VulkanInstance m_Instance;
		std::optional<VulkanDebugMessenger> m_DebugMessenger;
		VulkanSurface m_Surface;
		VulkanPhysicalDevice m_PhysicalDevice;
		VulkanDevice m_Device;
		std::vector<std::unique_ptr<VulkanMesh>> m_Meshes;
		std::vector<std::unique_ptr<VulkanTexturedMesh>> m_TexturedMeshes;
		std::vector<std::unique_ptr<VulkanTexture>> m_Textures;
		VulkanTextureDescriptors m_TextureDescriptors;
		std::optional<VulkanSwapchain> m_Swapchain;
		std::optional<VulkanClearPass> m_ClearPass;
		VulkanDrawQueue m_DrawQueue;
		std::uint64_t m_FramebufferRevision = 0;
		bool m_HasPresentedFrame = false;
	};

	VulkanContext::Impl::Impl(const Window& window)
		: m_Window(window),
		  m_ShaderDirectory(Paths::GetShaderPath()),
		  m_Instance(m_Window.GetRequiredVulkanInstanceExtensions()),
#ifdef RAVEN_ENABLE_VALIDATION
		  m_DebugMessenger(std::in_place, m_Instance),
#endif
		  m_Surface(m_Instance, m_Window),
		  m_PhysicalDevice(SelectVulkanPhysicalDevice(
			  m_Instance.Get(), m_Surface.Get())),
		  m_Device(m_PhysicalDevice),
		  m_TextureDescriptors(m_Device)
	{
		Log(LogLevel::Info,
			std::string("Selected GPU: ") +
			m_PhysicalDevice.Properties.deviceName);

		Log(LogLevel::Info,
			"Queue families: graphics=" +
			std::to_string(m_PhysicalDevice.QueueFamilies.Graphics.value()) +
			", present=" +
			std::to_string(m_PhysicalDevice.QueueFamilies.Present.value()));

		PrepareSwapchain();
		Log(LogLevel::Info, "Vulkan renderer context created");
	}

	void VulkanContext::Impl::PrepareSwapchain()
	{
		const auto framebuffer = m_Window.GetFramebufferState();
		if (!framebuffer.IsDrawable())
			return;
		if (m_Swapchain.has_value())
		{
			if (m_FramebufferRevision == framebuffer.Revision)
				return;
			ResetSwapchain();
		}

		m_Swapchain.emplace(m_Device, m_PhysicalDevice, m_Surface.Get(), framebuffer);
		if (m_Swapchain->Get() == VK_NULL_HANDLE)
		{
			m_Swapchain.reset();
			return;
		}
		try
		{
			m_ClearPass.emplace(m_Device, m_PhysicalDevice, *m_Swapchain,
				m_PhysicalDevice.QueueFamilies.Graphics.value(), m_TextureDescriptors.GetLayout(),
				m_ShaderDirectory);
		}
		catch (...)
		{
			m_Swapchain.reset();
			throw;
		}
		m_FramebufferRevision = framebuffer.Revision;

		const auto extent = m_Swapchain->GetExtent();
		Log(LogLevel::Info,
			"Swapchain created: " + std::to_string(extent.width) + " x " +
			std::to_string(extent.height) + ", images=" +
			std::to_string(m_Swapchain->GetImages().size()) + ", format=" +
			std::to_string(m_Swapchain->GetFormat()));
	}

	void VulkanContext::Impl::ResetSwapchain()
	{
		const VkResult result = vkDeviceWaitIdle(m_Device.Get());
		if (result != VK_SUCCESS)
			throw std::runtime_error("vkDeviceWaitIdle failed during swapchain recreation: " +
				std::to_string(result));
		m_ClearPass.reset();
		m_Swapchain.reset();
	}

	const VulkanMesh& VulkanContext::Impl::CreateMesh(std::span<const ColorVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		m_Meshes.push_back(std::make_unique<VulkanMesh>(m_Device, m_PhysicalDevice, vertices, indices));
		return *m_Meshes.back();
	}

	const VulkanTexture& VulkanContext::Impl::CreateTexture(const TextureDesc& description)
	{
		// Publish only after all creation succeeds; reserve before allocating a descriptor.
		if (m_Textures.size() == m_Textures.capacity())
			m_Textures.reserve(m_Textures.empty() ? 1 : m_Textures.size() * 2);
		auto texture = std::make_unique<VulkanTexture>(m_Device, m_PhysicalDevice, description);
		texture->SetDescriptorSet(m_TextureDescriptors.CreateBinding(*texture));
		m_Textures.push_back(std::move(texture));
		return *m_Textures.back();
	}

	const VulkanTexturedMesh& VulkanContext::Impl::CreateTexturedMesh(
		std::span<const TexturedVertex> vertices, std::span<const std::uint16_t> indices)
	{
		m_TexturedMeshes.push_back(std::make_unique<VulkanTexturedMesh>(
			m_Device, m_PhysicalDevice, vertices, indices));
		return *m_TexturedMeshes.back();
	}

	// Camera types provide view/projection values; frame and GPU ownership remain here.
	template<typename Camera>
	void VulkanContext::Impl::DrawFrame(const Camera& camera, std::span<const VulkanDraw> draws,
		std::span<const VulkanTexturedDraw> texturedDraws,
		std::span<const VulkanTransparentDraw> transparentDraws)
	{
		// Reject resources from other contexts before touching frame synchronization.
		const auto owns = [](const auto& resources, const auto& resource)
		{
			return std::any_of(resources.begin(), resources.end(),
				[&resource](const auto& owner) { return owner.get() == &resource; });
		};
		for (const auto& draw : draws)
			if (!owns(m_Meshes, draw.Mesh))
				throw std::invalid_argument("Color mesh does not belong to this renderer context");
		for (const auto& draw : texturedDraws)
			if (!owns(m_TexturedMeshes, draw.Mesh) || !owns(m_Textures, draw.Texture))
				throw std::invalid_argument("Textured mesh and texture must belong to this renderer context");
		for (const auto& submission : transparentDraws)
			if (!owns(m_TexturedMeshes, submission.Draw.Mesh) || !owns(m_Textures, submission.Draw.Texture))
				throw std::invalid_argument("Transparent mesh and texture must belong to this renderer context");
		if (!m_Window.GetFramebufferState().IsDrawable())
			return;
		PrepareSwapchain();
		if (!m_ClearPass.has_value())
			return;

		const auto extent = m_Swapchain->GetExtent();
		const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
		const Mat4 view = camera.GetView();
		const Mat4 viewProjection = camera.GetProjection(aspect) * view;
		// Visibility and ordering complete before acquisition or command recording.
		BuildVulkanDrawQueue(m_DrawQueue, view, viewProjection, draws, texturedDraws, transparentDraws);
		if (!m_ClearPass->Draw(*m_Swapchain,
			m_Device.GetGraphicsQueue(), m_Device.GetPresentQueue(),
			m_DrawQueue))
		{
			ResetSwapchain();
			return;
		}
		if (!m_HasPresentedFrame)
		{
			Log(LogLevel::Info, "First Vulkan frame presented");
			m_HasPresentedFrame = true;
		}
	}

	VulkanContext::Impl::~Impl()
	{
		const VkResult result = vkDeviceWaitIdle(m_Device.Get());
		if (result != VK_SUCCESS)
		{
			Log(LogLevel::Error,
				"vkDeviceWaitIdle failed during shutdown: " +
				std::to_string(result));
		}
	}

	VulkanContext::VulkanContext(const Window& window)
		: m_Impl(std::make_unique<Impl>(window))
	{
	}

	VulkanContext::~VulkanContext() = default;

	void VulkanContext::PrepareSwapchain()
	{
		m_Impl->PrepareSwapchain();
	}

	const VulkanMesh& VulkanContext::CreateMesh(std::span<const ColorVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		return m_Impl->CreateMesh(vertices, indices);
	}

	const VulkanTexture& VulkanContext::CreateTexture(const TextureDesc& description)
	{
		return m_Impl->CreateTexture(description);
	}

	const VulkanTexturedMesh& VulkanContext::CreateTexturedMesh(std::span<const TexturedVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		return m_Impl->CreateTexturedMesh(vertices, indices);
	}

	void VulkanContext::DrawFrame(const PerspectiveCamera& camera, std::span<const VulkanDraw> draws)
	{
		m_Impl->DrawFrame(camera, draws, {}, {});
	}

	void VulkanContext::DrawFrame(const PerspectiveCamera& camera, std::span<const VulkanDraw> colorDraws,
		std::span<const VulkanTexturedDraw> texturedDraws)
	{
		m_Impl->DrawFrame(camera, colorDraws, texturedDraws, {});
	}

	void VulkanContext::DrawFrame(const PerspectiveCamera& camera, std::span<const VulkanDraw> colorDraws,
		std::span<const VulkanTexturedDraw> opaqueDraws,
		std::span<const VulkanTransparentDraw> transparentDraws)
	{
		m_Impl->DrawFrame(camera, colorDraws, opaqueDraws, transparentDraws);
	}

	void VulkanContext::DrawFrame(const OrthographicCamera& camera, std::span<const VulkanDraw> draws)
	{
		m_Impl->DrawFrame(camera, draws, {}, {});
	}

	void VulkanContext::DrawFrame(const OrthographicCamera& camera, std::span<const VulkanDraw> colorDraws,
		std::span<const VulkanTexturedDraw> texturedDraws)
	{
		m_Impl->DrawFrame(camera, colorDraws, texturedDraws, {});
	}

	void VulkanContext::DrawFrame(const OrthographicCamera& camera, std::span<const VulkanDraw> colorDraws,
		std::span<const VulkanTexturedDraw> opaqueDraws,
		std::span<const VulkanTransparentDraw> transparentDraws)
	{
		m_Impl->DrawFrame(camera, colorDraws, opaqueDraws, transparentDraws);
	}
}

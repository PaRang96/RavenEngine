#pragma once

#include "Raven/Core/Api.hpp"

#include "Raven/Renderer/ColorVertex.hpp"
#include "Raven/Renderer/PerspectiveCamera.hpp"
#include "Raven/Renderer/TextureDesc.hpp"
#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace Raven
{
	class Window;
	class VulkanTexture;

	class VulkanContext
	{
	public:
		RAVEN_API explicit VulkanContext(const Window& window);
		RAVEN_API ~VulkanContext();

		RAVEN_API void PrepareSwapchain();
		// Meshes remain owned by this context until shutdown, after GPU work completes.
		RAVEN_API const VulkanMesh& CreateMesh(std::span<const ColorVertex> vertices,
			std::span<const std::uint16_t> indices);
		// Upload completes before return. Textures remain owned here until shutdown.
		RAVEN_API const VulkanTexture& CreateTexture(const TextureDesc& description);
		RAVEN_API void DrawFrame(const PerspectiveCamera& camera, std::span<const VulkanDraw> draws);

		VulkanContext(const VulkanContext&) = delete;
		VulkanContext& operator=(const VulkanContext&) = delete;

	private:
		// Vulkan owners and their layout stay inside the engine implementation.
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

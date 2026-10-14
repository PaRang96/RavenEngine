#pragma once

#include "Raven/Core/Api.hpp"

#include "Raven/Renderer/ColorVertex.hpp"
#include "Raven/Renderer/PerspectiveCamera.hpp"
#include "Raven/Renderer/OrthographicCamera.hpp"
#include "Raven/Renderer/TextureDesc.hpp"
#include "Raven/Renderer/TexturedVertex.hpp"
#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"

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
		// UV meshes use a separate type to enforce the matching vertex/pipeline layout.
		RAVEN_API const VulkanTexturedMesh& CreateTexturedMesh(std::span<const TexturedVertex> vertices,
			std::span<const std::uint16_t> indices);
		// Upload completes before return. Textures remain owned here until shutdown.
		RAVEN_API const VulkanTexture& CreateTexture(const TextureDesc& description);
		RAVEN_API void DrawFrame(const PerspectiveCamera& camera, std::span<const VulkanDraw> draws);
		// Opaque color draws precede opaque textured draws; depth resolves visibility.
		// All submitted resources must belong to this context.
		RAVEN_API void DrawFrame(const PerspectiveCamera& camera,
			std::span<const VulkanDraw> colorDraws, std::span<const VulkanTexturedDraw> texturedDraws);
		// Visible opaque draws precede transparent draws. Transparent order is ascending
		// layer/order, then far-to-near camera depth, with submission order breaking ties.
		RAVEN_API void DrawFrame(const PerspectiveCamera& camera,
			std::span<const VulkanDraw> colorDraws, std::span<const VulkanTexturedDraw> opaqueDraws,
			std::span<const VulkanTransparentDraw> transparentDraws);
		// Orthographic rendering shares the same resource, depth, visibility and ordering rules.
		RAVEN_API void DrawFrame(const OrthographicCamera& camera, std::span<const VulkanDraw> draws);
		RAVEN_API void DrawFrame(const OrthographicCamera& camera,
			std::span<const VulkanDraw> colorDraws, std::span<const VulkanTexturedDraw> texturedDraws);
		RAVEN_API void DrawFrame(const OrthographicCamera& camera,
			std::span<const VulkanDraw> colorDraws, std::span<const VulkanTexturedDraw> opaqueDraws,
			std::span<const VulkanTransparentDraw> transparentDraws);

		VulkanContext(const VulkanContext&) = delete;
		VulkanContext& operator=(const VulkanContext&) = delete;

	private:
		// Vulkan owners and their layout stay inside the engine implementation.
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

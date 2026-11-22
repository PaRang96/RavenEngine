#pragma once

#include "Raven/Core/Api.hpp"

#include "Raven/Renderer/ColorVertex.hpp"
#include "Raven/Renderer/SceneLighting.hpp"
#include "Raven/Renderer/PerspectiveCamera.hpp"
#include "Raven/Renderer/OrthographicCamera.hpp"
#include "Raven/Renderer/TextureDesc.hpp"
#include "Raven/Renderer/TextureHandle.hpp"
#include "Raven/Renderer/TexturedVertex.hpp"
#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"
#include "Raven/Renderer/CameraView.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <cstdint>
#include <memory>
#include <span>

namespace Raven
{
	class Window;
	class VulkanTexture;

	struct VulkanResourceStats
	{
		std::size_t Meshes = 0;
		std::size_t Textures = 0;
		std::size_t ManagedMeshes = 0;
		std::size_t ManagedTextures = 0;
		std::size_t RetiredTextures = 0;
		std::size_t MeshSlots = 0;
		std::size_t TextureSlots = 0;
		std::size_t DescriptorBindings = 0;
		std::size_t DescriptorPools = 0;
	};

	class VulkanContext
	{
	public:
		RAVEN_API explicit VulkanContext(const Window& window);
		RAVEN_API ~VulkanContext();

		RAVEN_API void PrepareSwapchain();

		// Meshes remain owned by this context until shutdown, after GPU work completes.
		RAVEN_API const VulkanMesh& CreateMesh(std::span<const ColorVertex> vertices,
			std::span<const std::uint32_t> indices);

		// Value handles can be copied into ECS data; geometry is shared, not copied.
		RAVEN_API MeshHandle CreateMeshHandle(std::span<const ColorVertex> vertices,
			std::span<const std::uint32_t> indices);

		// Null/foreign/expired-context handles resolve to null. Borrow only while this context lives.
		RAVEN_API const VulkanMesh* ResolveMesh(MeshHandle mesh) const noexcept;
		// Two-sided exact triangle hit, in world units, using current CPU/GPU mesh contents.
		RAVEN_API std::optional<float> IntersectMesh(MeshHandle mesh, const Mat4& model, const Ray& ray) const;

		// Rendering thread, between DrawFrame calls. The mesh reference and handle stay valid.
		// A failed replacement leaves the current geometry active.
		RAVEN_API void ReplaceMesh(MeshHandle mesh, std::span<const ColorVertex> vertices,
			std::span<const std::uint32_t> indices);

		// Managed resources are borrowed only while their owner holds the handle. Release invalidates
		// the handle immediately; submitted GPU work retains the required native resources.
		RAVEN_API MeshHandle CreateManagedMesh(std::span<const ColorVertex> vertices,
			std::span<const std::uint32_t> indices);
		RAVEN_API TextureHandle CreateManagedTexture(const TextureDesc& description);
		RAVEN_API const VulkanTexture* ResolveTexture(TextureHandle texture) const noexcept;
		RAVEN_API bool ReleaseMesh(MeshHandle mesh) noexcept;
		RAVEN_API bool ReleaseTexture(TextureHandle texture) noexcept;
		RAVEN_API void CollectResources() noexcept;
		RAVEN_API VulkanResourceStats GetResourceStats() const noexcept;

		// Prepared uploads complete earlier graphics work before these nonthrowing commits.
		// Both handles must be live managed resources in this context. Identity is unchanged.
		RAVEN_API bool SwapMeshContents(MeshHandle left, MeshHandle right) noexcept;
		RAVEN_API bool SwapTextureContents(TextureHandle left, TextureHandle right) noexcept;

		// UV meshes use a separate type to enforce the matching vertex/pipeline layout.
		RAVEN_API const VulkanTexturedMesh& CreateTexturedMesh(std::span<const TexturedVertex> vertices,
			std::span<const std::uint16_t> indices);

		// Upload completes before return. Textures remain owned here until shutdown.
		RAVEN_API const VulkanTexture& CreateTexture(const TextureDesc& description);

		// Rendering thread, between DrawFrame calls. A failed upload leaves the texture unchanged.
		// Existing texture references continue to identify the same context-owned object.
		RAVEN_API void ReplaceTexture(const VulkanTexture& texture, const TextureDesc& description);

		// Between frames on the rendering thread. Words must be validated and compatible
		// with the textured fragment interface. False means no drawable render pass yet.
		RAVEN_API bool ReplaceTexturedFragment(std::span<const std::uint32_t> words);

		// Opt in to the development watcher for the configured textured fragment shader.
		RAVEN_API void WatchTexturedFragment();
		RAVEN_API void WatchLitFragment();

		using CameraViewBuilder = std::function<std::optional<CameraView>(
			std::uint32_t width, std::uint32_t height)>;
		
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
		struct RenderedCameraView
		{
			CameraView Camera;
			std::uint32_t Width;
			std::uint32_t Height;
		};

		// Returns the camera and drawable extent used to present this frame.
		RAVEN_API std::optional<RenderedCameraView> DrawFrame(const CameraViewBuilder& buildView,
			std::span<const VulkanDraw> draws,
			std::span<const VulkanTexturedDraw> texturedDraws,
			std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting = {});

		VulkanContext(const VulkanContext&) = delete;
		VulkanContext& operator=(const VulkanContext&) = delete;

	private:
		// Vulkan owners and their layout stay inside the engine implementation.
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

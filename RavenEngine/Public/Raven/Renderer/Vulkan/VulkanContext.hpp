#pragma once

#include "Raven/Core/Api.hpp"

#include "Raven/Renderer/ColorVertex.hpp"
#include "Raven/Renderer/DebugLines.hpp"
#include "Raven/Renderer/Overlay.hpp"
#include "Raven/Renderer/MeshHandle.hpp"
#include "Raven/Renderer/SceneLighting.hpp"
#include "Raven/Renderer/PerspectiveCamera.hpp"
#include "Raven/Renderer/RenderView.hpp"
#include "Raven/Renderer/RendererConfig.hpp"
#include "Raven/Renderer/OrthographicCamera.hpp"
#include "Raven/Renderer/TextureDesc.hpp"
#include "Raven/Renderer/TextureHandle.hpp"
#include "Raven/Renderer/TexturedVertex.hpp"
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
	class VulkanMesh;
	class RenderWorld;

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
		// Bindless elements in use per class (B0 contract 8).
		std::size_t BindlessEngine = 0;
		std::size_t BindlessContent = 0;
	};

	class VulkanContext
	{
	public:
		RAVEN_API explicit VulkanContext(const Window& window);
		RAVEN_API VulkanContext(const Window& window, const RendererConfig& config);
		RAVEN_API ~VulkanContext();

		// True when the framebuffer is drawable and a swapchain is ready to render (R7). A minimized
		// window keeps its swapchain and returns false.
		RAVEN_API bool PrepareSwapchain();

		// Null/foreign/expired-context handles resolve to null. Borrow only while this context lives.
		RAVEN_API const VulkanMesh* ResolveMesh(MeshHandle mesh) const noexcept;

		// Managed resources are borrowed only while their owner holds the handle. Release invalidates
		// the handle immediately; submitted GPU work retains the required native resources.
		RAVEN_API MeshHandle CreateManagedMesh(std::span<const ColorVertex> vertices,
			std::span<const std::uint32_t> indices);
		RAVEN_API TextureHandle CreateManagedTexture(const TextureDesc& description, TextureUse use = TextureUse::Content);
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

		// Development: recompile every engine shader (vertex, fragment and compute) when it or an
		// include changes, and publish compatible results between frames (docs/HotReloadDesign.md, H2).
		RAVEN_API void WatchShaders();

		// How lit meshes are drawn; debug views need no reload (docs/HotReloadDesign.md, H3).
		// False when the device cannot draw the view (wireframe needs fillModeNonSolid).
		RAVEN_API bool SetRenderView(RenderView view);
		RAVEN_API RenderView GetRenderView() const noexcept;
		RAVEN_API bool IsRenderViewAvailable(RenderView view) const noexcept;

		using CameraViewBuilder = std::function<std::optional<CameraView>(
			std::uint32_t width, std::uint32_t height)>;
		
		struct RenderedCameraView
		{
			CameraView Camera;
			std::uint32_t Width;
			std::uint32_t Height;
		};

		// Returns the camera and drawable extent used to present this frame. Visible opaque textured
		// draws precede transparent draws; transparent order is ascending layer and order, then
		// far-to-near camera depth, with submission order breaking ties. All submitted resources
		// must belong to this context.
		// Lit meshes come from world, culled and drawn on the GPU (docs/GpuDrivenDesign.md, G5);
		// null draws none. The world's mesh handles and textures must belong to this context.
		// Debug lines (vertex pairs) are drawn depth-tested against the scene; the overlay's screen-space
		// quads are drawn last, after its texture regions are written (docs/UIDesign.md, section 4).
		RAVEN_API std::optional<RenderedCameraView> DrawFrame(const CameraViewBuilder& buildView,
			RenderWorld* world,
			std::span<const VulkanTexturedDraw> texturedDraws,
			std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting = {},
			std::span<const DebugLineVertex> debugLines = {}, const OverlayFrame& overlay = {});
		// The element of a texture in the bindless array that overlay quads name; zero (white) when
		// the handle does not resolve.
		RAVEN_API std::uint32_t GetBindlessIndex(TextureHandle texture) const noexcept;
		// The current drawable size in pixels; zero before the first swapchain.
		RAVEN_API std::pair<std::uint32_t, std::uint32_t> GetDrawableSize() const noexcept;

		VulkanContext(const VulkanContext&) = delete;
		VulkanContext& operator=(const VulkanContext&) = delete;

	private:
		// Vulkan owners and their layout stay inside the engine implementation.
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

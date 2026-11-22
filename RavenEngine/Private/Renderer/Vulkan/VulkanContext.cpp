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
#include "Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Renderer/Shaders/FragmentShaderWatch.hpp"
#include "Core/PathText.hpp"
#include "Core/FrameTrace.hpp"
#include "ShaderWatchConfig.hpp"
#include "Renderer/Vulkan/VulkanDrawQueue.hpp"

#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Window.hpp"
#include "Raven/Renderer/CameraView.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <format>
#include <future>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Raven
{
	namespace
	{
		template<typename Resource>
		bool OwnsResource(const std::vector<std::unique_ptr<Resource>>& resources, const Resource& resource)
		{
			for (const auto& owner : resources)
			{
				if (owner.get() == &resource)
					return true;
			}
			return false;
		}

		template<typename Resource>
		struct ResourceSlot
		{
			std::shared_ptr<Resource> Owner;
			std::uint64_t Generation = 0;
			bool Managed = false;
			bool Alive = false;
		};

		template<typename Resource>
		bool OwnsResource(const std::vector<ResourceSlot<Resource>>& resources, const Resource& resource)
		{
			for (const auto& slot : resources)
				if (slot.Alive && slot.Owner.get() == &resource)
					return true;
			return false;
		}

		template<typename Resource>
		std::size_t ReserveResourceSlot(std::vector<ResourceSlot<Resource>>& resources)
		{
			for (std::size_t index = 0; index < resources.size(); ++index)
				if (!resources[index].Owner && resources[index].Generation != std::numeric_limits<std::uint64_t>::max())
					return index;
			resources.emplace_back();
			return resources.size() - 1;
		}

		std::uint64_t AllocateMeshContextId()
		{
			static std::mutex mutex;
			static std::uint64_t lastId = 0;
			const std::scoped_lock lock(mutex);
			if (lastId == std::numeric_limits<std::uint64_t>::max())
				throw std::overflow_error("Mesh context identity exhausted");
			return ++lastId;
		}
	}

	struct VulkanContext::Impl
	{
		explicit Impl(const Window& window);
		~Impl();

		void PrepareSwapchain();
		void ResetSwapchain();
		struct FragmentProgram;
		void ApplyLitShaderReload();
		void ApplyTexturedShaderReload();
		void WatchFragment(FragmentProgram& program, FragmentShaderKind kind);
		void CollectResources() noexcept;
		std::size_t CreateMeshSlot(std::span<const ColorVertex> vertices,
			std::span<const std::uint16_t> indices, bool managed);
		std::size_t CreateTextureSlot(const TextureDesc& description, bool managed);
		const VulkanMesh& CreateMesh(std::span<const ColorVertex> vertices,
			std::span<const std::uint16_t> indices);
		const VulkanTexturedMesh& CreateTexturedMesh(std::span<const TexturedVertex> vertices,
			std::span<const std::uint16_t> indices);
		const VulkanTexture& CreateTexture(const TextureDesc& description);
		void ReplaceTexture(const VulkanTexture& texture, const TextureDesc& description);
		template<typename Camera>
		void DrawFrame(const Camera& camera, std::span<const VulkanDraw> draws,
			std::span<const VulkanTexturedDraw> texturedDraws,
			std::span<const VulkanTransparentDraw> transparentDraws);

		std::optional<RenderedCameraView> DrawFrameWithView(const CameraViewBuilder& buildView,
			std::span<const VulkanDraw> draws,
			std::span<const VulkanTexturedDraw> texturedDraws,
			std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting = {});

		const std::uint64_t m_MeshContextId = AllocateMeshContextId();
		const Window& m_Window;
		const std::filesystem::path m_ShaderDirectory;
		struct ShaderReloadTiming
		{
			double CompileMs;
			double PipelineMs;
		};
		struct FragmentProgram
		{
			const std::vector<std::uint32_t> Vertex;
			std::vector<std::uint32_t> Fragment;
			std::unique_ptr<FragmentShaderWatch> Watcher;
			std::optional<ShaderReloadTiming> Timing;
		};

		// Owners are destroyed in reverse declaration order.
		VulkanInstance m_Instance;
		std::optional<VulkanDebugMessenger> m_DebugMessenger;
		VulkanSurface m_Surface;
		VulkanPhysicalDevice m_PhysicalDevice;
		VulkanDevice m_Device;
		std::vector<ResourceSlot<VulkanMesh>> m_Meshes;
		std::vector<std::unique_ptr<VulkanTexturedMesh>> m_TexturedMeshes;
		std::vector<ResourceSlot<VulkanTexture>> m_Textures;
		VulkanTextureDescriptors m_TextureDescriptors;
		const VulkanTexture* m_WhiteTexture = nullptr;
		std::optional<VulkanSwapchain> m_Swapchain;
		std::optional<VulkanClearPass> m_ClearPass;
		VulkanDrawQueue m_DrawQueue;
		std::uint64_t m_FramebufferRevision = 0;
		bool m_HasPresentedFrame = false;

		FragmentProgram m_Lit;
		FragmentProgram m_Textured;
		struct LitPipelineResult
		{
			std::shared_ptr<const VulkanColorPipeline> Pipeline;
			double BuildMs;
		};
		struct LitPipelineBuild
		{
			std::shared_ptr<FragmentShaderWatch::Candidate> Shader;
			VulkanClearPass::LitPipelineFactory Factory;
			std::future<LitPipelineResult> Result;
		};
		std::optional<FragmentShaderWatch::Candidate> m_LitQueued;
		std::optional<LitPipelineBuild> m_LitBuild;
		// Pipelines kept across swapchain recreation while attachment formats match (R2).
		std::optional<VulkanClearPass::Pipelines> m_RetainedPipelines;
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
		  m_TextureDescriptors(m_Device),
		  m_Lit{ VulkanShaderModule::ReadWords(m_ShaderDirectory / "Color.vert.spv"),
			  VulkanShaderModule::ReadWords(m_ShaderDirectory / "Color.frag.spv"), {}, {} },
		  m_Textured{ VulkanShaderModule::ReadWords(m_ShaderDirectory / "Texture.vert.spv"),
			  VulkanShaderModule::ReadWords(m_ShaderDirectory / "Texture.frag.spv"), {}, {} }
	{
		Log(LogLevel::Info,
			std::string("Selected GPU: ") +
			m_PhysicalDevice.Properties.deviceName);

		Log(LogLevel::Info,
			"Vulkan " + std::to_string(VK_API_VERSION_MAJOR(m_PhysicalDevice.Properties.apiVersion)) + "." +
			std::to_string(VK_API_VERSION_MINOR(m_PhysicalDevice.Properties.apiVersion)) +
			" device; M3 features: descriptor indexing " + (m_PhysicalDevice.DescriptorIndexing ? "yes" : "no") +
			", draw indirect count " + (m_PhysicalDevice.DrawIndirectCount ? "yes" : "no"));

		Log(LogLevel::Info,
			"Queue families: graphics=" +
			std::to_string(m_PhysicalDevice.QueueFamilies.Graphics.value()) +
			", present=" +
			std::to_string(m_PhysicalDevice.QueueFamilies.Present.value()));

		const std::array<std::byte, 4> white{ std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255} };
		m_WhiteTexture = &CreateTexture({ 1, 1, white, TextureColorSpace::Linear });
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
				m_Lit.Vertex, m_Lit.Fragment, m_Textured.Vertex, m_Textured.Fragment,
				m_RetainedPipelines ? &*m_RetainedPipelines : nullptr);
			m_RetainedPipelines.reset();
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

	void VulkanContext::Impl::ApplyLitShaderReload()
	{
		auto& program = m_Lit;
		if (!program.Watcher)
			return;
		if (auto candidate = program.Watcher->TakeCandidate())
			m_LitQueued = std::move(candidate);

		try
		{
			if (m_LitBuild)
			{
				if (m_LitBuild->Result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
					return;
				auto build = std::move(*m_LitBuild);
				m_LitBuild.reset();
				auto result = build.Result.get();
				if (!m_LitQueued && program.Watcher->IsCurrent(*build.Shader))
				{
					if (m_ClearPass->IsCurrent(build.Factory))
					{
						m_ClearPass->UseLitPipeline(std::move(result.Pipeline));
						program.Fragment.swap(build.Shader->Words);
						program.Timing = ShaderReloadTiming{ build.Shader->CompileTime.count(), result.BuildMs };
					}
					else
						m_LitQueued = std::move(*build.Shader);
				}
			}
			if (auto candidate = std::exchange(m_LitQueued, std::nullopt);
				candidate && candidate->Words != program.Fragment && program.Watcher->IsCurrent(*candidate))
			{
				auto shader = std::make_shared<FragmentShaderWatch::Candidate>(std::move(*candidate));
				const auto factory = m_ClearPass->GetLitPipelineFactory();
				// A resize may retire this pass while the worker still prepares its pipeline.
				auto result = std::async(std::launch::async, [factory, shader, vertex = std::span(program.Vertex)]
				{
					const auto start = std::chrono::steady_clock::now();
					auto pipeline = factory.Create(vertex, shader->Words);
					const std::chrono::duration<double, std::milli> time = std::chrono::steady_clock::now() - start;
					return LitPipelineResult{ std::move(pipeline), time.count() };
				});
				m_LitBuild.emplace(LitPipelineBuild{ std::move(shader), factory, std::move(result) });
			}
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Warning, "Cannot replace shader pipelines for " +
				PathToUtf8(program.Watcher->GetSource()) + ": " + error.what());
		}
	}

	void VulkanContext::Impl::ApplyTexturedShaderReload()
	{
		auto& program = m_Textured;
		if (!program.Watcher)
			return;
		auto candidate = program.Watcher->TakeCandidate();
		if (!candidate || candidate->Words == program.Fragment)
			return;
		try
		{
			const auto start = std::chrono::steady_clock::now();
			const auto canPublish = [&] { return program.Watcher->IsCurrent(*candidate); };
			if (!m_ClearPass->ReplaceTexturedPipelines(program.Vertex, candidate->Words, canPublish))
				return;
			program.Fragment.swap(candidate->Words);
			const std::chrono::duration<double, std::milli> pipeline = std::chrono::steady_clock::now() - start;
			program.Timing = ShaderReloadTiming{ candidate->CompileTime.count(), pipeline.count() };
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Warning, "Cannot replace shader pipelines for " +
				PathToUtf8(program.Watcher->GetSource()) + ": " + error.what());
		}
	}

	void VulkanContext::Impl::ResetSwapchain()
	{
		const VkResult result = vkDeviceWaitIdle(m_Device.Get());
		if (result != VK_SUCCESS)
			throw std::runtime_error("vkDeviceWaitIdle failed during swapchain recreation: " +
				std::to_string(result));
		if (m_ClearPass)
			m_RetainedPipelines = m_ClearPass->GetPipelines();
		m_ClearPass.reset();
		m_Swapchain.reset();
	}

	const VulkanMesh& VulkanContext::Impl::CreateMesh(std::span<const ColorVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		return *m_Meshes[CreateMeshSlot(vertices, indices, false)].Owner;
	}

	std::size_t VulkanContext::Impl::CreateMeshSlot(std::span<const ColorVertex> vertices,
		std::span<const std::uint16_t> indices, bool managed)
	{
		const auto index = ReserveResourceSlot(m_Meshes);
		auto owner = std::make_shared<VulkanMesh>(m_Device, m_PhysicalDevice, vertices, indices);
		auto& slot = m_Meshes[index];
		slot.Owner = std::move(owner);
		++slot.Generation;
		slot.Alive = true;
		slot.Managed = managed;
		return index;
	}

	const VulkanTexture& VulkanContext::Impl::CreateTexture(const TextureDesc& description)
	{
		return *m_Textures[CreateTextureSlot(description, false)].Owner;
	}

	std::size_t VulkanContext::Impl::CreateTextureSlot(const TextureDesc& description, bool managed)
	{
		CollectResources();
		const auto index = ReserveResourceSlot(m_Textures);
		auto texture = std::make_shared<VulkanTexture>(m_Device, m_PhysicalDevice, description);
		texture->SetDescriptorSet(m_TextureDescriptors.CreateBinding(*texture));
		auto& slot = m_Textures[index];
		slot.Owner = std::move(texture);
		++slot.Generation;
		slot.Alive = true;
		slot.Managed = managed;
		return index;
	}

	void VulkanContext::Impl::CollectResources() noexcept
	{
		if (m_ClearPass)
			m_ClearPass->CollectCompletedResources();
		for (auto& slot : m_Textures)
			if (!slot.Alive && slot.Owner && slot.Owner.use_count() == 1)
			{
				m_TextureDescriptors.ReleaseBinding(slot.Owner->GetDescriptorSet());
				slot.Owner.reset();
			}
	}

	void VulkanContext::Impl::ReplaceTexture(const VulkanTexture& texture, const TextureDesc& description)
	{
		const auto found = std::find_if(m_Textures.begin(), m_Textures.end(),
			[&texture](const auto& slot) { return slot.Alive && slot.Owner.get() == &texture; });
		if (found == m_Textures.end())
			throw std::invalid_argument("Texture does not belong to this renderer context");

		VulkanTexture replacement(m_Device, m_PhysicalDevice, description);
		// The upload fence covers earlier draws on the same graphics queue.
		// Reuse the descriptor set and destroy the old image with the temporary.
		m_TextureDescriptors.WriteBinding(texture.GetDescriptorSet(), replacement);
		found->Owner->SwapImage(replacement);
	}

	const VulkanTexturedMesh& VulkanContext::Impl::CreateTexturedMesh(
		std::span<const TexturedVertex> vertices, std::span<const std::uint16_t> indices)
	{
		m_TexturedMeshes.push_back(std::make_unique<VulkanTexturedMesh>(
			m_Device, m_PhysicalDevice, vertices, indices));
		return *m_TexturedMeshes.back();
	}

	template<typename Camera>
	void VulkanContext::Impl::DrawFrame(const Camera& camera,
		std::span<const VulkanDraw> draws,
		std::span<const VulkanTexturedDraw> texturedDraws,
		std::span<const VulkanTransparentDraw> transparentDraws)
	{
		const CameraViewBuilder buildView = [&camera](std::uint32_t width, std::uint32_t height)
		{
			const float aspect = static_cast<float>(width) / static_cast<float>(height);
			return CameraView{
				.View = camera.GetView(),
				.Projection = camera.GetProjection(aspect)
			};
		};
		DrawFrameWithView(buildView, draws, texturedDraws, transparentDraws);
	}

	std::optional<VulkanContext::RenderedCameraView> VulkanContext::Impl::DrawFrameWithView(
		const CameraViewBuilder& buildView,
		std::span<const VulkanDraw> draws,
		std::span<const VulkanTexturedDraw> texturedDraws,
		std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting)
	{
		const auto frameStart = std::chrono::steady_clock::now();
		auto* metrics = GetFrameMetrics();
		FrameSection preparation(metrics ? &metrics->RenderPrepare : nullptr);
		CollectResources();
		// Reject resources from other contexts before touching frame synchronization.
		for (const auto& draw : draws)
			if (!OwnsResource(m_Meshes, draw.Mesh) ||
				(draw.Texture && !OwnsResource(m_Textures, *draw.Texture)))
				throw std::invalid_argument("Color mesh does not belong to this renderer context");
		for (const auto& draw : texturedDraws)
			if (!OwnsResource(m_TexturedMeshes, draw.Mesh) || !OwnsResource(m_Textures, draw.Texture))
				throw std::invalid_argument("Textured mesh and texture must belong to this renderer context");
		for (const auto& submission : transparentDraws)
			if (!OwnsResource(m_TexturedMeshes, submission.Draw.Mesh) || !OwnsResource(m_Textures, submission.Draw.Texture))
				throw std::invalid_argument("Transparent mesh and texture must belong to this renderer context");
		if (!m_Window.GetFramebufferState().IsDrawable())
			return std::nullopt;
		PrepareSwapchain();
		if (!m_ClearPass.has_value() || !buildView)
			return std::nullopt;

		const auto extent = m_Swapchain->GetExtent();
		const auto cameraView = buildView(extent.width, extent.height);
		if (!cameraView)
			return std::nullopt;

		ApplyLitShaderReload();
		ApplyTexturedShaderReload();
		const Mat4& view = cameraView->View;
		const Mat4 viewProjection = cameraView->Projection * view;

		// Visibility and ordering complete before acquisition or command recording.
		BuildVulkanDrawQueue(m_DrawQueue, view, viewProjection,
			draws, texturedDraws, transparentDraws, lighting);
			
		std::vector<std::shared_ptr<const VulkanTexture>> retainedTextures;
		const auto retain = [&](const VulkanTexture* texture)
		{
			if (!texture)
				return;
			const auto found = std::find_if(m_Textures.begin(), m_Textures.end(), [texture](const auto& slot)
			{
				return slot.Alive && slot.Owner.get() == texture;
			});
			retainedTextures.push_back(found->Owner);
		};
		for (const auto* draw : m_DrawQueue.Color)
			retain(draw->Texture);
		for (const auto& draw : m_DrawQueue.OpaqueTexture)
			retain(&draw.Draw->Texture);
		for (const auto& draw : m_DrawQueue.Transparent)
			retain(&draw.Draw->Texture);
		if (metrics)
		{
			metrics->VisibleMeshes = m_DrawQueue.Color.size();
			metrics->DrawCalls = m_DrawQueue.Color.size() + m_DrawQueue.OpaqueBatches.size() +
				m_DrawQueue.TransparentBatches.size();
		}
		preparation.End();
		if (!m_ClearPass->Draw(*m_Swapchain,
			m_Device.GetGraphicsQueue(), m_Device.GetPresentQueue(),
			m_DrawQueue, *m_WhiteTexture, retainedTextures, metrics))
		{
			ResetSwapchain();
			return std::nullopt;
		}
		if (metrics)
			metrics->Presented = TraceNanoseconds(TraceClock::now().time_since_epoch());
		if (!m_HasPresentedFrame)
		{
			Log(LogLevel::Info, "First Vulkan frame presented");
			m_HasPresentedFrame = true;
		}
		const auto reportShaderReload = [&](const char* label, FragmentProgram& program)
		{
			if (!program.Timing)
				return;
			const auto present = std::chrono::steady_clock::now();
			const std::chrono::duration<double, std::milli> frameWork = present - frameStart;
			Log(LogLevel::Info, std::format(
				"{} fragment reload presented: compile {:.3f} ms, pipelines {:.3f} ms, frame {:.3f} ms, clock {} ns",
				label, program.Timing->CompileMs, program.Timing->PipelineMs, frameWork.count(),
				std::chrono::duration_cast<std::chrono::nanoseconds>(present.time_since_epoch()).count()));
			program.Timing.reset();
		};
		reportShaderReload("Lit", m_Lit);
		reportShaderReload("Textured", m_Textured);
		return RenderedCameraView{ *cameraView, extent.width, extent.height };
	}

	VulkanContext::Impl::~Impl()
	{
		m_LitBuild.reset();
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

	MeshHandle VulkanContext::CreateMeshHandle(std::span<const ColorVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		const auto index = m_Impl->CreateMeshSlot(vertices, indices, false);
		return MeshHandle(m_Impl->m_MeshContextId, index, m_Impl->m_Meshes[index].Generation);
	}

	void VulkanContext::ReplaceMesh(MeshHandle mesh, std::span<const ColorVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		if (!ResolveMesh(mesh))
			throw std::invalid_argument("Mesh handle does not belong to this renderer context");

		m_Impl->m_Meshes[static_cast<std::size_t>(mesh.m_Index)].Owner->Replace(
			m_Impl->m_Device, m_Impl->m_PhysicalDevice, vertices, indices);
	}

	const VulkanMesh* VulkanContext::ResolveMesh(MeshHandle mesh) const noexcept
	{
		if (mesh.m_Context != m_Impl->m_MeshContextId || mesh.m_Index >= m_Impl->m_Meshes.size())
			return nullptr;
		const auto& slot = m_Impl->m_Meshes[static_cast<std::size_t>(mesh.m_Index)];
		return slot.Alive && slot.Generation == mesh.m_Generation ? slot.Owner.get() : nullptr;
	}

	const VulkanTexture& VulkanContext::CreateTexture(const TextureDesc& description)
	{
		return m_Impl->CreateTexture(description);
	}

	MeshHandle VulkanContext::CreateManagedMesh(std::span<const ColorVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		const auto index = m_Impl->CreateMeshSlot(vertices, indices, true);
		return MeshHandle(m_Impl->m_MeshContextId, index, m_Impl->m_Meshes[index].Generation);
	}

	TextureHandle VulkanContext::CreateManagedTexture(const TextureDesc& description)
	{
		const auto index = m_Impl->CreateTextureSlot(description, true);
		return TextureHandle(m_Impl->m_MeshContextId, index, m_Impl->m_Textures[index].Generation);
	}

	const VulkanTexture* VulkanContext::ResolveTexture(TextureHandle texture) const noexcept
	{
		if (texture.m_Context != m_Impl->m_MeshContextId || texture.m_Index >= m_Impl->m_Textures.size())
			return nullptr;
		const auto& slot = m_Impl->m_Textures[static_cast<std::size_t>(texture.m_Index)];
		return slot.Alive && slot.Generation == texture.m_Generation ? slot.Owner.get() : nullptr;
	}

	bool VulkanContext::ReleaseMesh(MeshHandle mesh) noexcept
	{
		if (!ResolveMesh(mesh))
			return false;
		auto& slot = m_Impl->m_Meshes[static_cast<std::size_t>(mesh.m_Index)];
		if (!slot.Managed)
			return false;
		slot.Alive = false;
		slot.Owner.reset();
		return true;
	}

	bool VulkanContext::ReleaseTexture(TextureHandle texture) noexcept
	{
		if (!ResolveTexture(texture))
			return false;
		auto& slot = m_Impl->m_Textures[static_cast<std::size_t>(texture.m_Index)];
		if (!slot.Managed)
			return false;
		slot.Alive = false;
		m_Impl->CollectResources();
		return true;
	}

	void VulkanContext::CollectResources() noexcept
	{
		m_Impl->CollectResources();
	}

	bool VulkanContext::SwapMeshContents(MeshHandle left, MeshHandle right) noexcept
	{
		if (!ResolveMesh(left) || !ResolveMesh(right))
			return false;
		auto& a = m_Impl->m_Meshes[static_cast<std::size_t>(left.m_Index)];
		auto& b = m_Impl->m_Meshes[static_cast<std::size_t>(right.m_Index)];
		if (!a.Managed || !b.Managed)
			return false;
		a.Owner->SwapData(*b.Owner);
		return true;
	}

	bool VulkanContext::SwapTextureContents(TextureHandle left, TextureHandle right) noexcept
	{
		if (!ResolveTexture(left) || !ResolveTexture(right))
			return false;
		auto& a = m_Impl->m_Textures[static_cast<std::size_t>(left.m_Index)];
		auto& b = m_Impl->m_Textures[static_cast<std::size_t>(right.m_Index)];
		if (!a.Managed || !b.Managed)
			return false;
		a.Owner->SwapImage(*b.Owner);
		m_Impl->m_TextureDescriptors.WriteBinding(a.Owner->GetDescriptorSet(), *a.Owner);
		m_Impl->m_TextureDescriptors.WriteBinding(b.Owner->GetDescriptorSet(), *b.Owner);
		return true;
	}

	VulkanResourceStats VulkanContext::GetResourceStats() const noexcept
	{
		VulkanResourceStats result;
		result.MeshSlots = m_Impl->m_Meshes.size();
		result.TextureSlots = m_Impl->m_Textures.size();
		for (const auto& slot : m_Impl->m_Meshes)
			if (slot.Alive)
			{
				++result.Meshes;
				result.ManagedMeshes += slot.Managed;
			}
		for (const auto& slot : m_Impl->m_Textures)
			if (slot.Alive)
			{
				++result.Textures;
				result.ManagedTextures += slot.Managed;
			}
			else if (slot.Owner)
				++result.RetiredTextures;
		result.DescriptorBindings = m_Impl->m_TextureDescriptors.GetBindingCount();
		result.DescriptorPools = m_Impl->m_TextureDescriptors.GetPoolCount();
		return result;
	}

	void VulkanContext::Impl::WatchFragment(FragmentProgram& program, FragmentShaderKind kind)
	{
#ifdef _WIN32
		if (program.Watcher)
			return;
		const bool lit = kind == FragmentShaderKind::Lit;
		const auto source = std::filesystem::path(ShaderWatchConfig::SourceDirectory) /
			(lit ? ShaderWatchConfig::LitSource : ShaderWatchConfig::TexturedSource);
		program.Watcher = std::make_unique<FragmentShaderWatch>(source, kind);
		Log(LogLevel::Info, std::string(lit ? "Watching lit fragment shader " : "Watching textured fragment shader ") +
			PathToUtf8(program.Watcher->GetSource()));
#else
		throw std::runtime_error("Live shader compilation currently requires Windows");
#endif
	}

	void VulkanContext::WatchLitFragment()
	{
		m_Impl->WatchFragment(m_Impl->m_Lit, FragmentShaderKind::Lit);
	}

	void VulkanContext::WatchTexturedFragment()
	{
		m_Impl->WatchFragment(m_Impl->m_Textured, FragmentShaderKind::Textured);
	}

	bool VulkanContext::ReplaceTexturedFragment(std::span<const std::uint32_t> words)
	{
		CheckFragmentShaderInterface(words, FragmentShaderKind::Textured);
		m_Impl->PrepareSwapchain();
		if (!m_Impl->m_Window.GetFramebufferState().IsDrawable() || !m_Impl->m_ClearPass)
			return false;
		std::vector<std::uint32_t> candidate(words.begin(), words.end());
		m_Impl->m_ClearPass->ReplaceTexturedPipelines(m_Impl->m_Textured.Vertex, candidate);
		m_Impl->m_Textured.Fragment.swap(candidate);
		return true;
	}

	void VulkanContext::ReplaceTexture(const VulkanTexture& texture, const TextureDesc& description)
	{
		m_Impl->ReplaceTexture(texture, description);
	}

	const VulkanTexturedMesh& VulkanContext::CreateTexturedMesh(std::span<const TexturedVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		return m_Impl->CreateTexturedMesh(vertices, indices);
	}

	std::optional<VulkanContext::RenderedCameraView> VulkanContext::DrawFrame(
		const CameraViewBuilder& buildView,
			std::span<const VulkanDraw> draws,
			std::span<const VulkanTexturedDraw> texturedDraws,
			std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting)
	{
		return m_Impl->DrawFrameWithView(buildView, draws, texturedDraws, transparentDraws, lighting);
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

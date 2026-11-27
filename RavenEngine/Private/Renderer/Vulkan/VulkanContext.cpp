#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include "Renderer/Vulkan/VulkanInstance.hpp"
#include "Renderer/Vulkan/VulkanMemory.hpp"
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
#include "Renderer/Vulkan/VulkanFrameTimeline.hpp"
#include "Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Renderer/Shaders/ShaderLibrary.hpp"
#include "Renderer/Vulkan/FragmentShaderInterface.hpp"
#include "Renderer/Vulkan/ShaderInterface.hpp"
#include "Renderer/Vulkan/VulkanFrameData.hpp"
#include "Core/PathText.hpp"
#include "Core/FrameTrace.hpp"
#include "Renderer/Vulkan/VulkanDrawQueue.hpp"
#include "Renderer/Vulkan/VulkanGpuScene.hpp"
#include "Renderer/RenderWorldState.hpp"

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

		const char* PresentModeName(PresentMode mode) noexcept
		{
			switch (mode)
			{
			case PresentMode::Mailbox: return "mailbox";
			case PresentMode::Immediate: return "immediate";
			default: return "fifo";
			}
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
		Impl(const Window& window, const RendererConfig& config);
		~Impl();

		bool PrepareSwapchain();
		void ResetSwapchain();
		void ApplyShaderReloads();
		void UpdateLitBuild();
		VulkanClearPass::LitShaders GetLitShaders() const;
		void CollectResources() noexcept;
		std::size_t CreateMeshSlot(std::span<const ColorVertex> vertices,
			std::span<const std::uint32_t> indices, bool managed);
		std::size_t CreateTextureSlot(const TextureDesc& description, bool managed, TextureUse use = TextureUse::Engine);
		const VulkanTexturedMesh& CreateTexturedMesh(std::span<const TexturedVertex> vertices,
			std::span<const std::uint16_t> indices);
		const VulkanTexture& CreateTexture(const TextureDesc& description);

		std::optional<RenderedCameraView> DrawFrameWithView(const CameraViewBuilder& buildView,
			RenderWorld* world, const VulkanGpuScene::MeshResolver& resolveMesh,
			std::span<const VulkanTexturedDraw> texturedDraws,
			std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting = {},
			std::span<const DebugLineVertex> debugLines = {}, const OverlayFrame& overlay = {});

		const std::uint64_t m_MeshContextId = AllocateMeshContextId();
		const Window& m_Window;
		const RendererConfig m_Config;
		const std::filesystem::path m_ShaderDirectory;
		// Every engine program's SPIR-V; watched compiles replace it (docs/HotReloadDesign.md, H2).
		ShaderLibrary m_Shaders;
		// Published shader changes, logged once their first frame is presented.
		struct ShaderReloadReport
		{
			const char* Group;
			std::string Programs;
			std::string Trigger;
			double CompileMs;
			double PipelineMs;
		};
		std::vector<ShaderReloadReport> m_ShaderReports;
		RenderView m_View = RenderView::Shaded;

		// Owners are destroyed in reverse declaration order.
		VulkanInstance m_Instance;
		std::optional<VulkanDebugMessenger> m_DebugMessenger;
		VulkanSurface m_Surface;
		VulkanPhysicalDevice m_PhysicalDevice;
		VulkanDevice m_Device;
		// Mesh data returns its ranges here, including retired data dropped by the timeline (R6).
		VulkanMeshPool m_MeshPool;
		std::vector<ResourceSlot<VulkanMesh>> m_Meshes;
		std::vector<std::unique_ptr<VulkanTexturedMesh>> m_TexturedMeshes;
		std::vector<ResourceSlot<VulkanTexture>> m_Textures;
		VulkanTextureDescriptors m_TextureDescriptors;
		// Pipelines retained across passes use it, so it outlives the timeline and the pass (R4).
		VulkanFrameLayout m_FrameLayout;
		const VulkanTexture* m_WhiteTexture = nullptr;
		// Outlives the pass and swapchain; retired resources drop before the stores above (R3).
		VulkanFrameTimeline m_Timeline;
		// GPU-driven lit meshes (docs/GpuDrivenDesign.md, G2/G5); the pass borrows it.
		VulkanGpuScene m_GpuScene;
		// Bumped when managed mesh contents are swapped or released; the GPU scene's resident mesh
		// table re-resolves on a change (B0 contract 3).
		std::uint64_t m_MeshRevision = 0;
		std::optional<VulkanSwapchain> m_Swapchain;
		// The overlay's texture regions for the next frame, resolved from their handles.
		std::vector<VulkanDrawQueue::TextureUpload> m_ResolvedUploads;
		std::optional<VulkanClearPass> m_ClearPass;
		VulkanDrawQueue m_DrawQueue;
		std::uint64_t m_FramebufferRevision = 0;
		bool m_HasPresentedFrame = false;

		// Lit pipelines (every view) build on a worker; a resize may retire the pass meanwhile.
		struct LitPipelineResult
		{
			std::shared_ptr<const VulkanClearPass::LitPipelines> Pipelines;
			double BuildMs;
		};
		struct LitPipelineBuild
		{
			std::vector<ShaderLibrary::Candidate> Shaders;
			VulkanClearPass::LitPipelineFactory Factory;
			std::future<LitPipelineResult> Result;
		};
		// Lit candidates waiting for the build in progress.
		std::vector<ShaderLibrary::Candidate> m_LitQueued;
		std::optional<LitPipelineBuild> m_LitBuild;
		// Pipelines kept across swapchain recreation while attachment formats match (R2).
		std::optional<VulkanClearPass::Pipelines> m_RetainedPipelines;
	};

	VulkanContext::Impl::Impl(const Window& window, const RendererConfig& config)
		: m_Window(window),
		  m_Config(config),
		  m_ShaderDirectory(Paths::GetShaderPath()),
		  m_Shaders(m_ShaderDirectory),
		  m_Instance(m_Window.GetRequiredVulkanInstanceExtensions()),
#ifdef RAVEN_ENABLE_VALIDATION
		  m_DebugMessenger(std::in_place, m_Instance),
#endif
		  m_Surface(m_Instance, m_Window),
		  m_PhysicalDevice(SelectVulkanPhysicalDevice(
			  m_Instance.Get(), m_Surface.Get(), config.Gpu)),
		  m_Device(m_PhysicalDevice, m_Instance.Get()),
		  m_MeshPool(m_Device, m_PhysicalDevice),
		  m_TextureDescriptors(m_Device),
		  m_FrameLayout(m_Device),
		  m_Timeline(m_Device),
		  m_GpuScene(m_Device, m_PhysicalDevice, m_Timeline,
			  m_Shaders.Get(ShaderProgram::Scatter), m_Shaders.Get(ShaderProgram::Cull))
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
		// Materials without a texture name bindless element 0 (G4).
		if (m_WhiteTexture->GetBindlessIndex() != 0)
			throw std::logic_error("The white texture must be bindless element 0");
		PrepareSwapchain();
		Log(LogLevel::Info, "Vulkan renderer context created");
	}

	bool VulkanContext::Impl::PrepareSwapchain()
	{
		const auto framebuffer = m_Window.GetFramebufferState();
		if (!framebuffer.IsDrawable())
			return false;
		if (m_Swapchain.has_value())
		{
			if (m_FramebufferRevision == framebuffer.Revision)
				return m_ClearPass.has_value();
			ResetSwapchain();
		}

		m_Swapchain.emplace(m_Device, m_PhysicalDevice, m_Surface.Get(), framebuffer, m_Config.Present);
		if (m_Swapchain->Get() == VK_NULL_HANDLE)
		{
			m_Swapchain.reset();
			return false;
		}
		try
		{
			m_ClearPass.emplace(m_Device, m_PhysicalDevice, *m_Swapchain,
				m_PhysicalDevice.QueueFamilies.Graphics.value(), m_TextureDescriptors.GetLayout(),
				m_TextureDescriptors.GetBindlessLayout(), m_FrameLayout.Get(), m_GpuScene, GetLitShaders(),
				m_Shaders.Get(ShaderProgram::TexturedVertex), m_Shaders.Get(ShaderProgram::TexturedFragment),
				m_Shaders.Get(ShaderProgram::DebugLineVertex), m_Shaders.Get(ShaderProgram::DebugLineFragment),
				m_Shaders.Get(ShaderProgram::OverlayVertex), m_Shaders.Get(ShaderProgram::OverlayFragment), m_Timeline,
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
			std::to_string(m_Swapchain->GetFormat()) + ", present=" + PresentModeName(m_Swapchain->GetPresentMode()));
		return true;
	}

	namespace
	{
		bool IsLitProgram(ShaderProgram program)
		{
			return program == ShaderProgram::LitVertex || program == ShaderProgram::LitFragment ||
				program == ShaderProgram::UnlitFragment || program == ShaderProgram::NormalsFragment ||
				program == ShaderProgram::WireframeFragment;
		}

		std::string ProgramNames(const std::vector<ShaderLibrary::Candidate>& candidates)
		{
			std::string names;
			for (const auto& candidate : candidates)
				names += (names.empty() ? "" : ", ") + std::string(ShaderLibrary::GetSourceName(candidate.Program));
			return names;
		}

		// The changed inputs behind a group of compiles, for example "Material.hlsli".
		std::string Triggers(const std::vector<ShaderLibrary::Candidate>& candidates)
		{
			std::vector<std::string> names;
			for (const auto& candidate : candidates)
				for (const auto& name : candidate.Trigger)
					if (std::find(names.begin(), names.end(), name) == names.end())
						names.push_back(name);
			std::string text;
			for (const auto& name : names)
				text += (text.empty() ? "" : ", ") + name;
			return text.empty() ? "-" : text;
		}

		double MaxCompile(const std::vector<ShaderLibrary::Candidate>& candidates)
		{
			double result = 0.0;
			for (const auto& candidate : candidates)
				result = std::max(result, candidate.CompileMs);
			return result;
		}

		// Replaces queued entries for the same program; the newest compile wins.
		void Merge(std::vector<ShaderLibrary::Candidate>& queue, std::vector<ShaderLibrary::Candidate> candidates)
		{
			for (auto& candidate : candidates)
			{
				std::erase_if(queue, [&](const ShaderLibrary::Candidate& queued) { return queued.Program == candidate.Program; });
				queue.push_back(std::move(candidate));
			}
		}

		std::span<const std::uint32_t> WordsOf(const ShaderLibrary& library,
			const std::vector<ShaderLibrary::Candidate>& candidates, ShaderProgram program)
		{
			for (const auto& candidate : candidates)
				if (candidate.Program == program)
					return candidate.Words;
			return library.Get(program);
		}

		// The fixed interfaces reloaded shaders are checked against (docs/HotReloadDesign.md, H2).
		ShaderInterface LitVertexInterface()
		{
			ShaderInterface result;
			result.Inputs = { { 0, 3 }, { 1, 3 }, { 2, 3 }, { 3, 2 } };
			// RavenFragmentInput in Raven/Material.hlsli.
			result.Outputs = { { 0, 3 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 3 }, { 5, 3 }, { 6, 1, true } };
			result.Buffers.push_back({ 1, 0, 0, { offsetof(FrameUniformData, ViewProjection), offsetof(FrameUniformData, View),
				offsetof(FrameUniformData, CameraPosition), offsetof(FrameUniformData, Ambient),
				offsetof(FrameUniformData, LightDirection), offsetof(FrameUniformData, LightColor) } });
			// The vertex stage sees the objects, visible slots and materials of the scene set.
			for (auto& buffer : VulkanGpuScene::GetBufferLayouts(2))
				if (buffer.Binding <= 2)
					result.Buffers.push_back(std::move(buffer));
			return result;
		}

		// Developer debug lines (FD-32 collider view): DebugLineVertex in, the frame uniform at set 0.
		ShaderInterface DebugLineVertexInterface()
		{
			ShaderInterface result;
			result.Inputs = { { 0, 3 }, { 1, 4 } };
			result.Outputs = { { 0, 4 } };
			result.Buffers.push_back({ 0, 0, 0, { offsetof(FrameUniformData, ViewProjection), offsetof(FrameUniformData, View),
				offsetof(FrameUniformData, CameraPosition), offsetof(FrameUniformData, Ambient),
				offsetof(FrameUniformData, LightDirection), offsetof(FrameUniformData, LightColor) } });
			return result;
		}

		// The shared 2D path: OverlayQuad instances in, no descriptor buffers (docs/UIDesign.md, section 4).
		ShaderInterface OverlayVertexInterface()
		{
			ShaderInterface result;
			result.Inputs = { { 0, 4 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 2, true } };
			result.Outputs = { { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 2, true } };
			// The push constant is the framebuffer size.
			result.PushConstantOffsets = { 0 };
			return result;
		}

		ShaderInterface TexturedVertexInterface()
		{
			ShaderInterface result;
			result.Inputs = { { 0, 3 }, { 1, 2 }, { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 } };
			result.Outputs = { { 0, 2 }, { 1, 4 } };
			return result;
		}

		void CheckProgram(ShaderProgram program, std::span<const std::uint32_t> words)
		{
			switch (program)
			{
			case ShaderProgram::LitVertex:
				CheckShaderInterface(words, LitVertexInterface());
				break;
			case ShaderProgram::TexturedVertex:
				CheckShaderInterface(words, TexturedVertexInterface());
				break;
			case ShaderProgram::TexturedFragment:
				CheckFragmentShaderInterface(words, FragmentShaderKind::Textured);
				break;
			case ShaderProgram::Scatter:
				CheckShaderInterface(words, VulkanGpuScene::GetScatterInterface());
				break;
			case ShaderProgram::Cull:
				CheckShaderInterface(words, VulkanGpuScene::GetCullInterface());
				break;
			case ShaderProgram::DebugLineVertex:
				CheckShaderInterface(words, DebugLineVertexInterface());
				break;
			case ShaderProgram::DebugLineFragment:
				// Its only input is the color the vertex stage writes. Not checked: pipeline creation does
				// not validate stage interfaces, and validation layers only log a mismatch.
				break;
			case ShaderProgram::OverlayVertex:
				CheckShaderInterface(words, OverlayVertexInterface());
				break;
			case ShaderProgram::OverlayFragment:
				// It reads what the vertex stage writes and the bindless array. Not checked: pipeline
				// creation does not validate stage interfaces, and validation layers only log a mismatch.
				break;
			case ShaderProgram::LitFragment:
			case ShaderProgram::UnlitFragment:
			case ShaderProgram::NormalsFragment:
			case ShaderProgram::WireframeFragment:
				// Every lit view's fragment shader uses the material interface.
				CheckFragmentShaderInterface(words, FragmentShaderKind::Lit);
				break;
			default:
				// A new program needs its own check and group here and in ApplyShaderReloads. This runs on
				// the reload worker, which reports the failed import; nothing is published.
				throw std::logic_error("unclassified shader program");
			}
		}
	}

	VulkanClearPass::LitShaders VulkanContext::Impl::GetLitShaders() const
	{
		return { m_Shaders.Get(ShaderProgram::LitVertex), m_Shaders.Get(ShaderProgram::LitFragment),
			m_Shaders.Get(ShaderProgram::UnlitFragment), m_Shaders.Get(ShaderProgram::NormalsFragment),
			m_Shaders.Get(ShaderProgram::WireframeFragment) };
	}

	void VulkanContext::Impl::ApplyShaderReloads()
	{
		if (!m_Shaders.IsWatching())
			return;
		// Candidates arrive only once every program has settled, so a header edit's dependents
		// publish in the same frame and a vertex/fragment pair never mixes versions (H2).
		std::vector<ShaderLibrary::Candidate> textured;
		std::vector<ShaderLibrary::Candidate> compute;
		std::vector<ShaderLibrary::Candidate> debugLines;
		std::vector<ShaderLibrary::Candidate> overlay;
		std::vector<ShaderLibrary::Candidate> lit;
		for (auto& candidate : m_Shaders.TakeReady())
		{
			if (IsLitProgram(candidate.Program))
				lit.push_back(std::move(candidate));
			else if (candidate.Program == ShaderProgram::Scatter || candidate.Program == ShaderProgram::Cull)
				compute.push_back(std::move(candidate));
			else if (candidate.Program == ShaderProgram::DebugLineVertex ||
				candidate.Program == ShaderProgram::DebugLineFragment)
				debugLines.push_back(std::move(candidate));
			else if (candidate.Program == ShaderProgram::OverlayVertex ||
				candidate.Program == ShaderProgram::OverlayFragment)
				overlay.push_back(std::move(candidate));
			else if (candidate.Program == ShaderProgram::TexturedVertex ||
				candidate.Program == ShaderProgram::TexturedFragment)
				textured.push_back(std::move(candidate));
			else
				// CheckProgram rejects unclassified programs first; never misroute one into a group.
				Log(LogLevel::Error, "Shader reload dropped: the program has no pipeline group");
		}
		const auto publish = [&](const char* group, std::vector<ShaderLibrary::Candidate>& candidates,
			const std::function<void()>& replace)
		{
			if (candidates.empty())
				return;
			try
			{
				const auto start = std::chrono::steady_clock::now();
				replace();
				const std::chrono::duration<double, std::milli> pipelines = std::chrono::steady_clock::now() - start;
				m_ShaderReports.push_back({ group, ProgramNames(candidates), Triggers(candidates), MaxCompile(candidates),
					pipelines.count() });
				for (auto& candidate : candidates)
					m_Shaders.Set(candidate.Program, std::move(candidate.Words), pipelines);
			}
			catch (const std::exception& error)
			{
				Log(LogLevel::Warning, std::string("Cannot replace ") + group + " pipelines for " +
					ProgramNames(candidates) + ": " + error.what());
			}
		};
		publish("Textured", textured, [&]
		{
			m_ClearPass->ReplaceTexturedPipelines(WordsOf(m_Shaders, textured, ShaderProgram::TexturedVertex),
				WordsOf(m_Shaders, textured, ShaderProgram::TexturedFragment));
		});
		publish("GPU scene", compute, [&]
		{
			m_GpuScene.ReplacePipelines(WordsOf(m_Shaders, compute, ShaderProgram::Scatter),
				WordsOf(m_Shaders, compute, ShaderProgram::Cull));
		});
		publish("Debug line", debugLines, [&]
		{
			m_ClearPass->ReplaceDebugLinePipeline(WordsOf(m_Shaders, debugLines, ShaderProgram::DebugLineVertex),
				WordsOf(m_Shaders, debugLines, ShaderProgram::DebugLineFragment));
		});
		publish("Overlay", overlay, [&]
		{
			m_ClearPass->ReplaceOverlayPipeline(WordsOf(m_Shaders, overlay, ShaderProgram::OverlayVertex),
				WordsOf(m_Shaders, overlay, ShaderProgram::OverlayFragment));
		});
		Merge(m_LitQueued, std::move(lit));
		UpdateLitBuild();
	}

	void VulkanContext::Impl::UpdateLitBuild()
	{
		try
		{
			if (m_LitBuild)
			{
				if (m_LitBuild->Result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
					return;
				auto build = std::move(*m_LitBuild);
				m_LitBuild.reset();
				auto result = build.Result.get();
				if (m_LitQueued.empty() && m_ClearPass->IsCurrent(build.Factory))
				{
					m_ClearPass->UseLitPipelines(std::move(result.Pipelines));
					m_ShaderReports.push_back({ "Lit", ProgramNames(build.Shaders), Triggers(build.Shaders),
						MaxCompile(build.Shaders), result.BuildMs });
					for (auto& candidate : build.Shaders)
						m_Shaders.Set(candidate.Program, std::move(candidate.Words),
							std::chrono::duration<double, std::milli>(result.BuildMs));
				}
				else
				{
					// Newer shaders or new attachment formats: rebuild with everything still unpublished.
					auto newer = std::move(m_LitQueued);
					m_LitQueued = std::move(build.Shaders);
					Merge(m_LitQueued, std::move(newer));
				}
			}
			if (m_LitQueued.empty())
				return;
			auto shaders = std::make_shared<std::vector<ShaderLibrary::Candidate>>(std::move(m_LitQueued));
			m_LitQueued.clear();
			// The worker reads its own copy of every lit program's words.
			auto words = std::make_shared<std::array<std::vector<std::uint32_t>, 5>>();
			const std::array programs{ ShaderProgram::LitVertex, ShaderProgram::LitFragment, ShaderProgram::UnlitFragment,
				ShaderProgram::NormalsFragment, ShaderProgram::WireframeFragment };
			for (std::size_t index = 0; index < programs.size(); ++index)
			{
				const auto source = WordsOf(m_Shaders, *shaders, programs[index]);
				(*words)[index].assign(source.begin(), source.end());
			}
			const auto factory = m_ClearPass->GetLitPipelineFactory();
			auto result = std::async(std::launch::async, [factory, words]
			{
				const auto start = std::chrono::steady_clock::now();
				auto pipelines = factory.Create({ (*words)[0], (*words)[1], (*words)[2], (*words)[3], (*words)[4] });
				const std::chrono::duration<double, std::milli> time = std::chrono::steady_clock::now() - start;
				return LitPipelineResult{ std::move(pipelines), time.count() };
			});
			m_LitBuild.emplace(LitPipelineBuild{ std::move(*shaders), factory, std::move(result) });
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Warning, std::string("Cannot replace lit pipelines: ") + error.what());
		}
	}

	void VulkanContext::Impl::ResetSwapchain()
	{
		CheckVulkanResult(vkDeviceWaitIdle(m_Device.Get()), "vkDeviceWaitIdle (swapchain recreation)");
		if (m_ClearPass)
			m_RetainedPipelines = m_ClearPass->GetPipelines();
		m_ClearPass.reset();
		m_Swapchain.reset();
		m_Timeline.Collect();
	}

	std::size_t VulkanContext::Impl::CreateMeshSlot(std::span<const ColorVertex> vertices,
		std::span<const std::uint32_t> indices, bool managed)
	{
		const auto index = ReserveResourceSlot(m_Meshes);
		auto owner = std::make_shared<VulkanMesh>(m_MeshPool, m_Timeline, vertices, indices);
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

	std::size_t VulkanContext::Impl::CreateTextureSlot(const TextureDesc& description, bool managed, TextureUse use)
	{
		CollectResources();
		const auto index = ReserveResourceSlot(m_Textures);
		auto texture = std::make_shared<VulkanTexture>(m_Device, m_PhysicalDevice, description);
		texture->SetDescriptorSet(m_TextureDescriptors.CreateBinding(*texture, use));
		auto& slot = m_Textures[index];
		slot.Owner = std::move(texture);
		++slot.Generation;
		slot.Alive = true;
		slot.Managed = managed;
		return index;
	}

	void VulkanContext::Impl::CollectResources() noexcept
	{
		// A released texture's slot keeps its owner until the retirement queue drops its copy.
		m_Timeline.Collect();
		for (auto& slot : m_Textures)
			if (!slot.Alive && slot.Owner && slot.Owner.use_count() == 1)
			{
				m_TextureDescriptors.ReleaseBinding(*slot.Owner);
				slot.Owner.reset();
			}
	}

	const VulkanTexturedMesh& VulkanContext::Impl::CreateTexturedMesh(
		std::span<const TexturedVertex> vertices, std::span<const std::uint16_t> indices)
	{
		m_TexturedMeshes.push_back(std::make_unique<VulkanTexturedMesh>(
			m_Device, m_PhysicalDevice, vertices, indices));
		return *m_TexturedMeshes.back();
	}

	std::optional<VulkanContext::RenderedCameraView> VulkanContext::Impl::DrawFrameWithView(
		const CameraViewBuilder& buildView, RenderWorld* world, const VulkanGpuScene::MeshResolver& resolveMesh,
		std::span<const VulkanTexturedDraw> texturedDraws,
		std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting,
		std::span<const DebugLineVertex> debugLines, const OverlayFrame& overlay)
	{
		const auto frameStart = std::chrono::steady_clock::now();
		auto* metrics = GetFrameMetrics();
		FrameSection preparation(metrics ? &metrics->RenderPrepare : nullptr);
		CollectResources();
		// Reject resources from other contexts before touching frame synchronization.
		for (const auto& draw : texturedDraws)
			if (!OwnsResource(m_TexturedMeshes, draw.Mesh) || !OwnsResource(m_Textures, draw.Texture))
				throw std::invalid_argument("Textured mesh and texture must belong to this renderer context");
		for (const auto& submission : transparentDraws)
			if (!OwnsResource(m_TexturedMeshes, submission.Draw.Mesh) || !OwnsResource(m_Textures, submission.Draw.Texture))
				throw std::invalid_argument("Transparent mesh and texture must belong to this renderer context");
		if (!PrepareSwapchain() || !buildView)
			return std::nullopt;

		const auto extent = m_Swapchain->GetExtent();
		const auto cameraView = buildView(extent.width, extent.height);
		if (!cameraView)
			return std::nullopt;

		ApplyShaderReloads();
		const Mat4& view = cameraView->View;
		const Mat4 viewProjection = cameraView->Projection * view;

		// Visibility and ordering complete before acquisition or command recording.
		BuildVulkanDrawQueue(m_DrawQueue, view, viewProjection, texturedDraws, transparentDraws, lighting);
		m_DrawQueue.DebugLines = debugLines;
		m_DrawQueue.Overlay = overlay.Quads;
		// Resolved by the public DrawFrame, which may read texture handles.
		m_DrawQueue.Uploads.swap(m_ResolvedUploads);
		m_ResolvedUploads.clear();
		// Lit meshes: plan the GPU scene's upload; culling happens on the GPU (G5).
		m_GpuScene.Plan(world ? &world->GetState() : nullptr, resolveMesh, m_MeshRevision);
		if (metrics)
			metrics->DrawCalls = m_GpuScene.GetDrawCount() * (m_View == RenderView::Wireframe ? 2 : 1) +
				m_DrawQueue.OpaqueBatches.size() + m_DrawQueue.TransparentBatches.size();
		preparation.End();
		if (!m_ClearPass->Draw(*m_Swapchain,
			m_Device.GetGraphicsQueue(), m_Device.GetPresentQueue(),
			m_DrawQueue, m_MeshPool, m_TextureDescriptors.GetBindlessSet(), metrics, m_View))
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
		// One line per published group: the H7 summary, with the presentation clock for timing.
		const auto present = std::chrono::steady_clock::now();
		const std::chrono::duration<double, std::milli> frameWork = present - frameStart;
		for (const auto& report : m_ShaderReports)
			Log(LogLevel::Info, std::format(
				"{} shader reload presented: {}; trigger {}; compile {:.3f} ms, pipelines {:.3f} ms, frame {:.3f} ms, clock {} ns",
				report.Group, report.Programs, report.Trigger, report.CompileMs, report.PipelineMs, frameWork.count(),
				std::chrono::duration_cast<std::chrono::nanoseconds>(present.time_since_epoch()).count()));
		m_ShaderReports.clear();
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
		: VulkanContext(window, RendererConfig{})
	{
	}

	VulkanContext::VulkanContext(const Window& window, const RendererConfig& config)
		: m_Impl(std::make_unique<Impl>(window, config))
	{
	}

	VulkanContext::~VulkanContext() = default;

	bool VulkanContext::PrepareSwapchain()
	{
		return m_Impl->PrepareSwapchain();
	}

	const VulkanMesh* VulkanContext::ResolveMesh(MeshHandle mesh) const noexcept
	{
		if (mesh.m_Context != m_Impl->m_MeshContextId || mesh.m_Index >= m_Impl->m_Meshes.size())
			return nullptr;
		const auto& slot = m_Impl->m_Meshes[static_cast<std::size_t>(mesh.m_Index)];
		return slot.Alive && slot.Generation == mesh.m_Generation ? slot.Owner.get() : nullptr;
	}

	MeshHandle VulkanContext::CreateManagedMesh(std::span<const ColorVertex> vertices,
		std::span<const std::uint32_t> indices)
	{
		const auto index = m_Impl->CreateMeshSlot(vertices, indices, true);
		return MeshHandle(m_Impl->m_MeshContextId, index, m_Impl->m_Meshes[index].Generation);
	}

	TextureHandle VulkanContext::CreateManagedTexture(const TextureDesc& description, TextureUse use)
	{
		const auto index = m_Impl->CreateTextureSlot(description, true, use);
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
		++m_Impl->m_MeshRevision;
		// Submitted frames may still read the mesh buffers (R3).
		m_Impl->m_Timeline.Retire(std::move(slot.Owner));
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
		// Submitted frames may still sample the image; the slot is freed once this copy drops (R3).
		m_Impl->m_Timeline.Retire(slot.Owner);
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
		++m_Impl->m_MeshRevision;
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
		m_Impl->m_TextureDescriptors.WriteBinding(*a.Owner, *a.Owner);
		m_Impl->m_TextureDescriptors.WriteBinding(*b.Owner, *b.Owner);
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
		result.BindlessEngine = m_Impl->m_TextureDescriptors.GetEngineElements();
		result.BindlessContent = m_Impl->m_TextureDescriptors.GetContentElements();
		const auto memory = m_Impl->m_Device.GetMemory().GetStats();
		result.GpuAllocations = memory.Allocations;
		result.GpuAllocatedBytes = static_cast<std::size_t>(memory.AllocatedBytes);
		result.GpuBlockBytes = static_cast<std::size_t>(memory.BlockBytes);
		return result;
	}

	void VulkanContext::WatchShaders()
	{
		m_Impl->m_Shaders.Watch(CheckProgram);
	}

	bool VulkanContext::IsRenderViewAvailable(RenderView view) const noexcept
	{
		return view != RenderView::Wireframe || m_Impl->m_PhysicalDevice.FillModeNonSolid;
	}

	bool VulkanContext::SetRenderView(RenderView view)
	{
		if (!IsRenderViewAvailable(view))
			return false;
		m_Impl->m_View = view;
		return true;
	}

	RenderView VulkanContext::GetRenderView() const noexcept
	{
		return m_Impl->m_View;
	}

	const VulkanTexturedMesh& VulkanContext::CreateTexturedMesh(std::span<const TexturedVertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		return m_Impl->CreateTexturedMesh(vertices, indices);
	}

	std::optional<VulkanContext::RenderedCameraView> VulkanContext::DrawFrame(
		const CameraViewBuilder& buildView, RenderWorld* world,
			std::span<const VulkanTexturedDraw> texturedDraws,
			std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting,
			std::span<const DebugLineVertex> debugLines, const OverlayFrame& overlay)
	{
		// Handle resolution belongs to the context, which MeshHandle befriends.
		const VulkanGpuScene::MeshResolver resolveMesh = [this](MeshHandle handle) -> const VulkanMeshData*
		{
			const auto* mesh = ResolveMesh(handle);
			return mesh ? mesh->GetData().get() : nullptr;
		};
		m_Impl->m_ResolvedUploads.clear();
		for (const auto& upload : overlay.Uploads)
		{
			const auto* texture = ResolveTexture(upload.Texture);
			const auto extent = texture ? texture->GetExtent() : VkExtent2D{};
			if (!texture || upload.Width == 0 || upload.Height == 0 || upload.X + upload.Width > extent.width ||
				upload.Y + upload.Height > extent.height ||
				upload.Pixels.size() != std::size_t{ upload.Width } * upload.Height * 4)
				throw std::invalid_argument("Overlay texture upload does not fit its texture");
			m_Impl->m_ResolvedUploads.push_back({ texture->GetImage(), upload.X, upload.Y, upload.Width, upload.Height,
				upload.Pixels });
		}
		return m_Impl->DrawFrameWithView(buildView, world, resolveMesh, texturedDraws, transparentDraws, lighting,
			debugLines, overlay);
	}

	std::uint32_t VulkanContext::GetBindlessIndex(TextureHandle texture) const noexcept
	{
		const auto* resolved = ResolveTexture(texture);
		return resolved ? resolved->GetBindlessIndex() : 0;
	}

	std::pair<std::uint32_t, std::uint32_t> VulkanContext::GetDrawableSize() const noexcept
	{
		if (!m_Impl->m_Swapchain)
			return { 0, 0 };
		const auto extent = m_Impl->m_Swapchain->GetExtent();
		return { extent.width, extent.height };
	}
}

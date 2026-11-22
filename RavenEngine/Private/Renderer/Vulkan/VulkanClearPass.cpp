#include "Renderer/Vulkan/VulkanClearPass.hpp"
#include "Renderer/Vulkan/VulkanTexturedMesh.hpp"
#include "Renderer/Vulkan/VulkanTexture.hpp"
#include "Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Core/FrameTrace.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	namespace
	{
		void CheckClearResult(VkResult result, const char* operation)
		{
			CheckVulkanResult(result, operation);
		}

		template<typename Handle>
		void CheckClearCreation(VkResult result, Handle& handle, const char* operation)
		{
			// Failed creation does not guarantee a usable output handle.
			if (result != VK_SUCCESS)
				handle = VK_NULL_HANDLE;
			CheckClearResult(result, operation);
		}

		void RecordTexturedBatches(VkCommandBuffer commands, VkBuffer instanceBuffer, VkDeviceSize instanceOffset,
			const VulkanTexturePipeline& pipeline, std::span<const TexturedBatch> batches)
		{
			for (const auto& batch : batches)
			{
				pipeline.Bind(commands, batch.Texture->GetDescriptorSet());
				const std::array<VkBuffer, 2> buffers
				{
					batch.Mesh->GetVertices().Get(), instanceBuffer
				};
				const std::array<VkDeviceSize, 2> offsets{ 0, instanceOffset };
				vkCmdBindVertexBuffers(commands, 0, static_cast<std::uint32_t>(buffers.size()),
					buffers.data(), offsets.data());
				vkCmdBindIndexBuffer(commands, batch.Mesh->GetIndices().Get(), 0, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(commands, batch.Mesh->GetIndexCount(), batch.InstanceCount,
					0, 0, batch.FirstInstance);
			}
		}
	}

	VulkanClearPass::LitPipelineFactory::LitPipelineFactory(const VulkanDevice& device,
		VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout textureLayout,
		VkDescriptorSetLayout frameLayout, VkDescriptorSetLayout sceneLayout)
		: m_Device(device), m_ColorFormat(colorFormat), m_DepthFormat(depthFormat), m_TextureLayout(textureLayout),
		  m_FrameLayout(frameLayout), m_SceneLayout(sceneLayout)
	{
	}

	std::shared_ptr<const VulkanColorPipeline> VulkanClearPass::LitPipelineFactory::Create(
		std::span<const std::uint32_t> vertex, std::span<const std::uint32_t> fragment) const
	{
		return std::make_shared<VulkanColorPipeline>(m_Device, m_ColorFormat, m_DepthFormat, m_TextureLayout,
			m_FrameLayout, m_SceneLayout, vertex, fragment);
	}

	VulkanClearPass::LitPipelineFactory VulkanClearPass::GetLitPipelineFactory() const
	{
		return { m_DeviceOwner, m_ColorFormat, m_DepthFormat, m_BindlessLayout, m_FrameLayout, m_Scene.GetLayout() };
	}

	VulkanClearPass::Pipelines VulkanClearPass::GetPipelines() const
	{
		return { m_LitPipeline, m_TexturedPipelines, m_ColorFormat, m_DepthFormat };
	}

	bool VulkanClearPass::IsCurrent(const LitPipelineFactory& factory) const noexcept
	{
		// A pipeline built before a resize stays valid while the attachment formats match.
		return factory.m_ColorFormat == m_ColorFormat && factory.m_DepthFormat == m_DepthFormat;
	}

	void VulkanClearPass::UseLitPipeline(std::shared_ptr<const VulkanColorPipeline> pipeline) noexcept
	{
		// Submitted frames may still use the previous pipeline (R3).
		m_Timeline.Retire(std::move(m_LitPipeline));
		m_LitPipeline = std::move(pipeline);
	}

	VulkanClearPass::TexturedPipelines::TexturedPipelines(const VulkanDevice& device,
		VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout textureLayout,
		const VulkanShaderModule& vertex, const VulkanShaderModule& fragment)
		: Opaque(device, colorFormat, depthFormat, textureLayout, vertex, fragment),
		  Transparent(device, colorFormat, depthFormat, textureLayout, vertex, fragment,
			  TexturePipelineMode::StraightAlpha)
	{
	}

	bool VulkanClearPass::ReplaceTexturedPipelines(std::span<const std::uint32_t> vertex,
		std::span<const std::uint32_t> fragment, const std::function<bool()>& canPublish)
	{
		const VulkanShaderModule vertexModule(m_DeviceOwner, vertex);
		const VulkanShaderModule fragmentModule(m_DeviceOwner, fragment);
		auto candidate = std::make_shared<TexturedPipelines>(m_DeviceOwner, m_ColorFormat, m_DepthFormat,
			m_TextureLayout, vertexModule, fragmentModule);
		if (canPublish && !canPublish())
			return false;
		m_Timeline.Retire(std::move(m_TexturedPipelines));
		m_TexturedPipelines = std::move(candidate);
		return true;
	}

	VulkanClearPass::VulkanClearPass(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice, const VulkanSwapchain& swapchain,
		std::uint32_t graphicsFamily, VkDescriptorSetLayout textureLayout, VkDescriptorSetLayout bindlessLayout,
		VkDescriptorSetLayout frameLayout, VulkanGpuScene& scene,
		std::span<const std::uint32_t> litVertex, std::span<const std::uint32_t> litFragment,
		std::span<const std::uint32_t> texturedVertex, std::span<const std::uint32_t> texturedFragment,
		VulkanFrameTimeline& timeline, const Pipelines* reuse)
		: m_DeviceOwner(device), m_PhysicalDevice(physicalDevice), m_Timeline(timeline),
		  m_Device(device.Get()), m_TextureLayout(textureLayout), m_BindlessLayout(bindlessLayout),
		  m_FrameLayout(frameLayout), m_Scene(scene), m_Extent(swapchain.GetExtent())
	{
		if (m_Device == VK_NULL_HANDLE || swapchain.Get() == VK_NULL_HANDLE ||
			swapchain.GetImageViews().empty() || m_Extent.width == 0 || m_Extent.height == 0)
			throw std::runtime_error("A drawable swapchain is required for the clear pass");

		try
		{
			m_ColorFormat = swapchain.GetFormat();
			m_DepthFormat = VulkanDepthAttachment::SelectFormat(physicalDevice);
			if (reuse && reuse->Lit && reuse->Textured &&
				reuse->ColorFormat == m_ColorFormat && reuse->DepthFormat == m_DepthFormat)
			{
				// Resizing keeps the attachment formats, so the existing pipelines remain valid.
				m_LitPipeline = reuse->Lit;
				m_TexturedPipelines = reuse->Textured;
			}
			else
			{
				UseLitPipeline(GetLitPipelineFactory().Create(litVertex, litFragment));
				ReplaceTexturedPipelines(texturedVertex, texturedFragment);
			}

			m_ColorImages = swapchain.GetImages();
			m_ColorViews = swapchain.GetImageViews();
			if (m_ColorImages.size() != m_ColorViews.size())
				throw std::runtime_error("Swapchain images and views do not match");
			m_DepthAttachments.reserve(m_ColorImages.size());
			for (std::size_t index = 0; index < m_ColorImages.size(); ++index)
				m_DepthAttachments.push_back(std::make_unique<VulkanDepthAttachment>(
					device, physicalDevice, m_Extent, m_DepthFormat));

			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			poolInfo.queueFamilyIndex = graphicsFamily;
			CheckClearCreation(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool),
				m_CommandPool, "vkCreateCommandPool");

			std::array<VkCommandBuffer, 2> commands{};
			VkCommandBufferAllocateInfo allocation{};
			allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocation.commandPool = m_CommandPool;
			allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocation.commandBufferCount = static_cast<std::uint32_t>(commands.size());
			CheckClearResult(vkAllocateCommandBuffers(m_Device, &allocation, commands.data()),
				"vkAllocateCommandBuffers");

			// One frame-uniform set per slot; each names its slot's upload ring (R4).
			const VkDescriptorPoolSize frameSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				static_cast<std::uint32_t>(m_Frames.size()) };
			VkDescriptorPoolCreateInfo framePoolInfo{};
			framePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			framePoolInfo.maxSets = static_cast<std::uint32_t>(m_Frames.size());
			framePoolInfo.poolSizeCount = 1;
			framePoolInfo.pPoolSizes = &frameSize;
			CheckClearCreation(vkCreateDescriptorPool(m_Device, &framePoolInfo, nullptr, &m_FramePool),
				m_FramePool, "vkCreateDescriptorPool (frame)");
			std::array<VkDescriptorSetLayout, 2> frameLayouts{};
			frameLayouts.fill(m_FrameLayout);
			std::array<VkDescriptorSet, 2> frameSets{};
			VkDescriptorSetAllocateInfo frameAllocation{};
			frameAllocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			frameAllocation.descriptorPool = m_FramePool;
			frameAllocation.descriptorSetCount = static_cast<std::uint32_t>(frameSets.size());
			frameAllocation.pSetLayouts = frameLayouts.data();
			CheckClearResult(vkAllocateDescriptorSets(m_Device, &frameAllocation, frameSets.data()),
				"vkAllocateDescriptorSets (frame)");

			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			for (std::size_t index = 0; index < m_Frames.size(); ++index)
			{
				auto& frame = m_Frames[index];
				frame.Commands = commands[index];
				frame.FrameSet = frameSets[index];
				CheckClearCreation(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr,
					&frame.ImageAvailable), frame.ImageAvailable, "vkCreateSemaphore (acquire)");
			}

			m_RenderFinished.resize(m_ColorImages.size(), VK_NULL_HANDLE);
			for (auto& semaphore : m_RenderFinished)
				CheckClearCreation(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore),
					semaphore, "vkCreateSemaphore (present)");
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	VulkanClearPass::~VulkanClearPass()
	{
		Destroy();
	}

	void VulkanClearPass::Destroy() noexcept
	{
		for (const auto semaphore : m_RenderFinished)
			if (semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(m_Device, semaphore, nullptr);
		for (const auto& frame : m_Frames)
			if (frame.ImageAvailable != VK_NULL_HANDLE)
				vkDestroySemaphore(m_Device, frame.ImageAvailable, nullptr);
		if (m_CommandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
		if (m_FramePool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(m_Device, m_FramePool, nullptr);
		if (m_Timestamps != VK_NULL_HANDLE)
			vkDestroyQueryPool(m_Device, m_Timestamps, nullptr);
		for (auto& frame : m_Frames)
			frame.Ring = {};
		m_TexturedPipelines.reset();
		m_LitPipeline.reset();
		m_DepthAttachments.clear();
	}

	VkDeviceSize VulkanClearPass::Upload(Frame& frame, const VulkanDrawQueue& queue)
	{
		const auto uniform = std::as_bytes(std::span(&queue.Frame, 1));
		const auto textured = std::as_bytes(std::span(queue.TexturedInstances));
		const VkDeviceSize required = VulkanUploadRing::Align(VulkanUploadRing::Align(uniform.size()) + textured.size()) +
			m_Scene.GetPlannedBytes();
		if (frame.Ring.Begin(m_DeviceOwner, m_PhysicalDevice, m_Timeline, required))
		{
			// The slot's previous submission has completed, so its set is not in use.
			const VkDescriptorBufferInfo buffer{ frame.Ring.Get(), 0, sizeof(FrameUniformData) };
			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = frame.FrameSet;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			write.pBufferInfo = &buffer;
			vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
		}
		// The frame set names offset 0, so the uniform is always pushed first.
		if (frame.Ring.Push(uniform) != 0)
			throw std::logic_error("The frame uniform must start the upload ring");
		const auto texturedOffset = frame.Ring.Push(textured);
		m_Scene.Upload(m_FrameIndex, frame.Ring);
		return texturedOffset;
	}

	void VulkanClearPass::PrepareTimestampQueries()
	{
		if (m_TimestampsChecked)
			return;
		m_TimestampsChecked = true;
		std::uint32_t count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice.Handle, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice.Handle, &count, families.data());
		m_TimestampBits = families.at(m_PhysicalDevice.QueueFamilies.Graphics.value()).timestampValidBits;
		if (m_TimestampBits == 0)
			return;
		VkQueryPoolCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = static_cast<std::uint32_t>(m_Frames.size() * 3);
		CheckClearCreation(vkCreateQueryPool(m_Device, &info, nullptr, &m_Timestamps),
			m_Timestamps, "vkCreateQueryPool (frame timestamps)");
	}

	void VulkanClearPass::ReadTimestamps(Frame& frame, FrameMetrics& metrics)
	{
		if (frame.TimestampFrame == 0)
			return;
		const auto first = static_cast<std::uint32_t>(m_FrameIndex * 3);
		std::array<std::uint64_t, 3> ticks{};
		// The slot's previous submission has already completed. Never wait on the query itself.
		const auto result = vkGetQueryPoolResults(m_Device, m_Timestamps, first, 3,
			sizeof(ticks), ticks.data(), sizeof(ticks[0]), VK_QUERY_RESULT_64_BIT);
		if (result == VK_SUCCESS)
		{
			const auto mask = std::numeric_limits<std::uint64_t>::max() >> (64 - m_TimestampBits);
			metrics.GpuFrame = frame.TimestampFrame;
			const double period = m_PhysicalDevice.Properties.limits.timestampPeriod;
			metrics.Gpu = static_cast<double>((ticks[2] - ticks[0]) & mask) * period;
			metrics.GpuCull = static_cast<double>((ticks[1] - ticks[0]) & mask) * period;
		}
		else if (result != VK_NOT_READY)
			CheckClearResult(result, "vkGetQueryPoolResults");
		frame.TimestampFrame = 0;
	}

	void VulkanClearPass::Record(Frame& frame, std::uint32_t imageIndex, const VulkanDrawQueue& queue,
		const VulkanMeshPool& meshes, VkDescriptorSet bindlessTextures, VkDeviceSize texturedOffset, bool timestamps)
	{
		const auto commands = frame.Commands;
		const auto instanceBuffer = frame.Ring.Get();
		CheckClearResult(vkResetCommandBuffer(commands, 0), "vkResetCommandBuffer");
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CheckClearResult(vkBeginCommandBuffer(commands, &beginInfo), "vkBeginCommandBuffer");
		const auto firstQuery = static_cast<std::uint32_t>(m_FrameIndex * 3);
		if (timestamps)
		{
			vkCmdResetQueryPool(commands, m_Timestamps, firstQuery, 3);
			vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_Timestamps, firstQuery);
		}
		// GPU-driven lit meshes: apply this frame's uploads and cull into indirect commands (G5).
		m_Scene.RecordPrepare(commands, m_FrameIndex, queue.Frame.ViewProjection);
		if (timestamps)
			vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, m_Timestamps, firstQuery + 1);

		const auto& vk = m_DeviceOwner.GetCommands();
		const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		const VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		const auto transition = [&vk, commands](std::span<const VkImageMemoryBarrier2KHR> barriers)
		{
			VkDependencyInfoKHR dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
			dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
			dependency.pImageMemoryBarriers = barriers.data();
			vk.PipelineBarrier2(commands, &dependency);
		};

		// The acquire semaphore is waited at color output, so the color transition starts
		// there. Depth was last written by an earlier frame's fragment tests on this image.
		std::array<VkImageMemoryBarrier2KHR, 2> begin{};
		for (auto& barrier : begin)
		{
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		}
		begin[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
		begin[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
		begin[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR;
		begin[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		begin[0].image = m_ColorImages[imageIndex];
		begin[0].subresourceRange = colorRange;
		begin[1].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR |
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT_KHR;
		begin[1].srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR;
		begin[1].dstStageMask = begin[1].srcStageMask;
		begin[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT_KHR |
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR;
		begin[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		begin[1].image = m_DepthAttachments[imageIndex]->GetImage();
		begin[1].subresourceRange = depthRange;
		transition(begin);

		VkRenderingAttachmentInfoKHR color{};
		color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		color.imageView = m_ColorViews[imageIndex];
		color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color.clearValue.color = { { 0.03f, 0.08f, 0.18f, 1.0f } };
		VkRenderingAttachmentInfoKHR depth{};
		depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		depth.imageView = m_DepthAttachments[imageIndex]->GetView();
		depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth.clearValue.depthStencil = { 1.0f, 0 };
		VkRenderingInfoKHR rendering{};
		rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
		rendering.renderArea.extent = m_Extent;
		rendering.layerCount = 1;
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments = &color;
		rendering.pDepthAttachment = &depth;
		vk.BeginRendering(commands, &rendering);
		const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_Extent.width),
			static_cast<float>(m_Extent.height), 0.0f, 1.0f };
		const VkRect2D scissor{ { 0, 0 }, m_Extent };
		vkCmdSetViewport(commands, 0, 1, &viewport);
		vkCmdSetScissor(commands, 0, 1, &scissor);
		// Every lit mesh lives in the two pools (R6) and is drawn by the culled indirect commands.
		m_Scene.RecordDraw(commands, m_FrameIndex, *m_LitPipeline, bindlessTextures, frame.FrameSet,
			meshes.GetVertices(), meshes.GetIndices());
		RecordTexturedBatches(commands, instanceBuffer, texturedOffset, m_TexturedPipelines->Opaque,
			queue.OpaqueBatches);
		RecordTexturedBatches(commands, instanceBuffer, texturedOffset, m_TexturedPipelines->Transparent,
			queue.TransparentBatches);
		vk.EndRendering(commands);
		m_Scene.RecordReadback(commands, m_FrameIndex);

		// Presentation waits on the render-finished semaphore, which the submission signals
		// after all commands, so the transition needs no later stage.
		VkImageMemoryBarrier2KHR present{};
		present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
		present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
		present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR;
		present.dstStageMask = VK_PIPELINE_STAGE_2_NONE_KHR;
		present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		present.image = m_ColorImages[imageIndex];
		present.subresourceRange = colorRange;
		transition(std::span(&present, 1));
		if (timestamps)
			vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_Timestamps, firstQuery + 2);
		CheckClearResult(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
	}

	bool VulkanClearPass::Draw(const VulkanSwapchain& swapchain, VkQueue graphics,
		VkQueue present, const VulkanDrawQueue& queue, const VulkanMeshPool& meshes,
		VkDescriptorSet bindlessTextures, FrameMetrics* metrics)
	{
		auto& frame = m_Frames[m_FrameIndex];
		constexpr auto timeout = std::numeric_limits<std::uint64_t>::max();
		{
			// Reuse the slot once its previous frame has completed on the timeline (R3).
			FrameSection wait(metrics ? &metrics->FenceWait : nullptr);
			m_Timeline.Wait(frame.Submitted);
		}
		FrameSection upload(metrics ? &metrics->RecordSubmit : nullptr);
		if (metrics)
		{
			PrepareTimestampQueries();
			ReadTimestamps(frame, *metrics);
			// Culled on the GPU; the slot's last submitted frame reports its visible count.
			metrics->VisibleMeshes = m_Scene.ReadVisible(m_FrameIndex);
		}
		// Write the frame's uniform and instances before acquiring the image.
		const auto texturedOffset = Upload(frame, queue);
		upload.End();

		std::uint32_t imageIndex = 0;
		FrameSection acquiring(metrics ? &metrics->AcquireWait : nullptr);
		const VkResult acquire = vkAcquireNextImageKHR(m_Device, swapchain.Get(), timeout,
			frame.ImageAvailable, VK_NULL_HANDLE, &imageIndex);
		acquiring.End();
		if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
			return false;
		if (acquire != VK_SUBOPTIMAL_KHR)
			CheckClearResult(acquire, "vkAcquireNextImageKHR");
		if (imageIndex >= m_ColorImages.size())
			throw std::runtime_error("Acquired swapchain image index is out of range");

		FrameSection submitting(metrics ? &metrics->RecordSubmit : nullptr);
		const bool timestamps = metrics && m_Timestamps != VK_NULL_HANDLE;
		Record(frame, imageIndex, queue, meshes, bindlessTextures, texturedOffset, timestamps);

		// The binary semaphores serve acquire and present; the timeline value marks this frame.
		VkSemaphoreSubmitInfoKHR acquired{};
		acquired.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
		acquired.semaphore = frame.ImageAvailable;
		acquired.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
		const std::uint64_t value = m_Timeline.GetNextValue();
		std::array<VkSemaphoreSubmitInfoKHR, 2> signals{};
		for (auto& signal : signals)
		{
			signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
			signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
		}
		signals[0].semaphore = m_RenderFinished[imageIndex];
		signals[1].semaphore = m_Timeline.Get();
		signals[1].value = value;
		VkCommandBufferSubmitInfoKHR commandInfo{};
		commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
		commandInfo.commandBuffer = frame.Commands;
		VkSubmitInfo2KHR submission{};
		submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR;
		submission.waitSemaphoreInfoCount = 1;
		submission.pWaitSemaphoreInfos = &acquired;
		submission.commandBufferInfoCount = 1;
		submission.pCommandBufferInfos = &commandInfo;
		submission.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signals.size());
		submission.pSignalSemaphoreInfos = signals.data();
		CheckClearResult(m_DeviceOwner.GetCommands().QueueSubmit2(graphics, 1, &submission, VK_NULL_HANDLE),
			"vkQueueSubmit2KHR");
		m_Timeline.MarkSubmitted();
		frame.Submitted = value;
		m_Scene.Commit(m_FrameIndex);
		frame.TimestampFrame = timestamps ? metrics->Start : 0;

		const VkSwapchainKHR handle = swapchain.Get();
		VkPresentInfoKHR presentation{};
		presentation.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentation.waitSemaphoreCount = 1;
		presentation.pWaitSemaphores = &m_RenderFinished[imageIndex];
		presentation.swapchainCount = 1;
		presentation.pSwapchains = &handle;
		presentation.pImageIndices = &imageIndex;
		submitting.End();
		FrameSection presenting(metrics ? &metrics->PresentWait : nullptr);
		const VkResult result = vkQueuePresentKHR(present, &presentation);
		presenting.End();
		m_FrameIndex = (m_FrameIndex + 1) % m_Frames.size();
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			return false;
		CheckClearResult(result, "vkQueuePresentKHR");
		return acquire != VK_SUBOPTIMAL_KHR;
	}
}

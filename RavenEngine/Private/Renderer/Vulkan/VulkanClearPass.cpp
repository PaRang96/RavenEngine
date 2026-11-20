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
			if (result != VK_SUCCESS)
				throw std::runtime_error(std::string(operation) + " failed: " +
					std::to_string(result));
		}

		template<typename Handle>
		void CheckClearCreation(VkResult result, Handle& handle, const char* operation)
		{
			// Failed creation does not guarantee a usable output handle.
			if (result != VK_SUCCESS)
				handle = VK_NULL_HANDLE;
			CheckClearResult(result, operation);
		}

		void RecordTexturedBatches(VkCommandBuffer commands, VkBuffer instanceBuffer,
			const VulkanTexturePipeline& pipeline, std::span<const TexturedBatch> batches)
		{
			for (const auto& batch : batches)
			{
				pipeline.Bind(commands, batch.Texture->GetDescriptorSet());
				const std::array<VkBuffer, 2> buffers
				{
					batch.Mesh->GetVertices().Get(), instanceBuffer
				};
				const std::array<VkDeviceSize, 2> offsets{ 0, 0 };
				vkCmdBindVertexBuffers(commands, 0, static_cast<std::uint32_t>(buffers.size()),
					buffers.data(), offsets.data());
				vkCmdBindIndexBuffer(commands, batch.Mesh->GetIndices().Get(), 0, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(commands, batch.Mesh->GetIndexCount(), batch.InstanceCount,
					0, 0, batch.FirstInstance);
			}
		}
	}

	struct VulkanClearPass::RenderPass
	{
		explicit RenderPass(VkDevice device) : Device(device) {}
		~RenderPass()
		{
			if (Handle != VK_NULL_HANDLE)
				vkDestroyRenderPass(Device, Handle, nullptr);
		}
		VkDevice Device;
		VkRenderPass Handle = VK_NULL_HANDLE;
	};

	VulkanClearPass::LitPipelineFactory::LitPipelineFactory(const VulkanDevice& device,
		std::shared_ptr<const RenderPass> pass, VkDescriptorSetLayout textureLayout)
		: m_Device(device), m_Pass(std::move(pass)), m_TextureLayout(textureLayout)
	{
	}

	std::shared_ptr<const VulkanColorPipeline> VulkanClearPass::LitPipelineFactory::Create(
		std::span<const std::uint32_t> vertex, std::span<const std::uint32_t> fragment) const
	{
		return std::make_shared<VulkanColorPipeline>(m_Device, m_Pass->Handle, m_TextureLayout, vertex, fragment);
	}

	VulkanClearPass::LitPipelineFactory VulkanClearPass::GetLitPipelineFactory() const
	{
		return { m_DeviceOwner, m_RenderPass, m_TextureLayout };
	}

	bool VulkanClearPass::IsCurrent(const LitPipelineFactory& factory) const noexcept
	{
		return factory.m_Pass == m_RenderPass;
	}

	void VulkanClearPass::UseLitPipeline(std::shared_ptr<const VulkanColorPipeline> pipeline) noexcept
	{
		m_LitPipeline = std::move(pipeline);
	}

	VulkanClearPass::TexturedPipelines::TexturedPipelines(const VulkanDevice& device,
		VkRenderPass renderPass, VkDescriptorSetLayout textureLayout,
		const VulkanShaderModule& vertex, const VulkanShaderModule& fragment)
		: Opaque(device, renderPass, textureLayout, vertex, fragment),
		  Transparent(device, renderPass, textureLayout, vertex, fragment, TexturePipelineMode::StraightAlpha)
	{
	}

	bool VulkanClearPass::ReplaceTexturedPipelines(std::span<const std::uint32_t> vertex,
		std::span<const std::uint32_t> fragment, const std::function<bool()>& canPublish)
	{
		const VulkanShaderModule vertexModule(m_DeviceOwner, vertex);
		const VulkanShaderModule fragmentModule(m_DeviceOwner, fragment);
		auto candidate = std::make_shared<TexturedPipelines>(m_DeviceOwner, m_RenderPass->Handle,
			m_TextureLayout, vertexModule, fragmentModule);
		if (canPublish && !canPublish())
			return false;
		m_TexturedPipelines = std::move(candidate);
		return true;
	}

	VulkanClearPass::VulkanClearPass(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice, const VulkanSwapchain& swapchain,
		std::uint32_t graphicsFamily, VkDescriptorSetLayout textureLayout,
		std::span<const std::uint32_t> litVertex, std::span<const std::uint32_t> litFragment,
		std::span<const std::uint32_t> texturedVertex, std::span<const std::uint32_t> texturedFragment)
		: m_DeviceOwner(device), m_PhysicalDevice(physicalDevice),
		  m_Device(device.Get()), m_TextureLayout(textureLayout), m_Extent(swapchain.GetExtent())
	{
		if (m_Device == VK_NULL_HANDLE || swapchain.Get() == VK_NULL_HANDLE ||
			swapchain.GetImageViews().empty() || m_Extent.width == 0 || m_Extent.height == 0)
			throw std::runtime_error("A drawable swapchain is required for the clear pass");

		try
		{
			const VkFormat depthFormat = VulkanDepthAttachment::SelectFormat(physicalDevice);
			VkAttachmentDescription color{};
			color.format = swapchain.GetFormat();
			color.samples = VK_SAMPLE_COUNT_1_BIT;
			color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			VkAttachmentDescription depth{};
			depth.format = depthFormat;
			depth.samples = VK_SAMPLE_COUNT_1_BIT;
			depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			const std::array<VkAttachmentDescription, 2> attachments{ color, depth };

			const VkAttachmentReference colorReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			const VkAttachmentReference depthReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorReference;
			subpass.pDepthStencilAttachment = &depthReference;

			VkSubpassDependency dependency{};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependency.dstStageMask = dependency.srcStageMask;
			dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo passInfo{};
			passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			passInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
			passInfo.pAttachments = attachments.data();
			passInfo.subpassCount = 1;
			passInfo.pSubpasses = &subpass;
			passInfo.dependencyCount = 1;
			passInfo.pDependencies = &dependency;
			m_RenderPass = std::make_shared<RenderPass>(m_Device);
			CheckClearCreation(vkCreateRenderPass(m_Device, &passInfo, nullptr, &m_RenderPass->Handle),
				m_RenderPass->Handle, "vkCreateRenderPass");
			UseLitPipeline(GetLitPipelineFactory().Create(litVertex, litFragment));
			ReplaceTexturedPipelines(texturedVertex, texturedFragment);

			m_Framebuffers.resize(swapchain.GetImageViews().size(), VK_NULL_HANDLE);
			m_DepthAttachments.reserve(m_Framebuffers.size());
			for (std::size_t index = 0; index < m_Framebuffers.size(); ++index)
			{
				m_DepthAttachments.push_back(std::make_unique<VulkanDepthAttachment>(
					device, physicalDevice, m_Extent, depthFormat));
				const std::array<VkImageView, 2> views
				{
					swapchain.GetImageViews()[index], m_DepthAttachments.back()->GetView()
				};
				VkFramebufferCreateInfo framebufferInfo{};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = m_RenderPass->Handle;
				framebufferInfo.attachmentCount = static_cast<std::uint32_t>(views.size());
				framebufferInfo.pAttachments = views.data();
				framebufferInfo.width = m_Extent.width;
				framebufferInfo.height = m_Extent.height;
				framebufferInfo.layers = 1;
				CheckClearCreation(vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr,
					&m_Framebuffers[index]), m_Framebuffers[index], "vkCreateFramebuffer");
			}

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

			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			for (std::size_t index = 0; index < m_Frames.size(); ++index)
			{
				auto& frame = m_Frames[index];
				frame.Commands = commands[index];
				CheckClearCreation(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr,
					&frame.ImageAvailable), frame.ImageAvailable, "vkCreateSemaphore (acquire)");
				CheckClearCreation(vkCreateFence(m_Device, &fenceInfo, nullptr, &frame.InFlight),
					frame.InFlight, "vkCreateFence");
			}

			m_RenderFinished.resize(m_Framebuffers.size(), VK_NULL_HANDLE);
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
		{
			if (frame.InFlight != VK_NULL_HANDLE)
				vkDestroyFence(m_Device, frame.InFlight, nullptr);
			if (frame.ImageAvailable != VK_NULL_HANDLE)
				vkDestroySemaphore(m_Device, frame.ImageAvailable, nullptr);
		}
		if (m_CommandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
		if (m_Timestamps != VK_NULL_HANDLE)
			vkDestroyQueryPool(m_Device, m_Timestamps, nullptr);
		for (auto& frame : m_Frames)
		{
			frame.TexturedInstances.reset();
			frame.MeshInstances.reset();
			frame.LitPipeline.reset();
			frame.Textured.reset();
		}
		m_TexturedPipelines.reset();
		m_LitPipeline.reset();
		for (const auto framebuffer : m_Framebuffers)
			if (framebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
		m_DepthAttachments.clear();
		m_RenderPass.reset();
	}

	void VulkanClearPass::UploadInstances(std::unique_ptr<VulkanBuffer>& buffer,
		std::span<const std::byte> bytes)
	{
		if (bytes.empty())
			return;
		constexpr VkDeviceSize maxBytes = std::numeric_limits<VkDeviceSize>::max();
		const auto required = static_cast<VkDeviceSize>(bytes.size());
		if (!buffer || buffer->GetSize() < required)
		{
			VkDeviceSize capacity = buffer ? buffer->GetSize() :
				64 * sizeof(TexturedInstanceData);
			while (capacity < required)
				capacity = capacity <= maxBytes / 2 ? capacity * 2 : required;
			auto replacement = std::make_unique<VulkanBuffer>(m_DeviceOwner, m_PhysicalDevice,
				capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
			replacement->Write(bytes);
			buffer = std::move(replacement);
		}
		else
		{
			buffer->Write(bytes);
		}
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
		info.queryCount = static_cast<std::uint32_t>(m_Frames.size() * 2);
		CheckClearCreation(vkCreateQueryPool(m_Device, &info, nullptr, &m_Timestamps),
			m_Timestamps, "vkCreateQueryPool (frame timestamps)");
	}

	void VulkanClearPass::ReadTimestamps(Frame& frame, FrameMetrics& metrics)
	{
		if (frame.TimestampFrame == 0)
			return;
		const auto first = static_cast<std::uint32_t>(m_FrameIndex * 2);
		std::array<std::uint64_t, 2> ticks{};
		// The existing slot fence has already completed. Never wait on the query itself.
		const auto result = vkGetQueryPoolResults(m_Device, m_Timestamps, first, 2,
			sizeof(ticks), ticks.data(), sizeof(ticks[0]), VK_QUERY_RESULT_64_BIT);
		if (result == VK_SUCCESS)
		{
			const auto mask = std::numeric_limits<std::uint64_t>::max() >> (64 - m_TimestampBits);
			metrics.GpuFrame = frame.TimestampFrame;
			metrics.Gpu = static_cast<double>((ticks[1] - ticks[0]) & mask) *
				m_PhysicalDevice.Properties.limits.timestampPeriod;
		}
		else if (result != VK_NOT_READY)
			CheckClearResult(result, "vkGetQueryPoolResults");
		frame.TimestampFrame = 0;
	}

	void VulkanClearPass::Record(Frame& frame, std::uint32_t imageIndex, const VulkanDrawQueue& queue,
		const VulkanTexture& white, bool timestamps)
	{
		const auto commands = frame.Commands;
		const auto instanceBuffer = frame.TexturedInstances ? frame.TexturedInstances->Get() : VK_NULL_HANDLE;
		CheckClearResult(vkResetCommandBuffer(commands, 0), "vkResetCommandBuffer");
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CheckClearResult(vkBeginCommandBuffer(commands, &beginInfo), "vkBeginCommandBuffer");
		const auto firstQuery = static_cast<std::uint32_t>(m_FrameIndex * 2);
		if (timestamps)
		{
			vkCmdResetQueryPool(commands, m_Timestamps, firstQuery, 2);
			vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_Timestamps, firstQuery);
		}

		std::array<VkClearValue, 2> clear{};
		clear[0].color = { { 0.03f, 0.08f, 0.18f, 1.0f } };
		clear[1].depthStencil = { 1.0f, 0 };
		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = m_RenderPass->Handle;
		passInfo.framebuffer = m_Framebuffers[imageIndex];
		passInfo.renderArea.extent = m_Extent;
		passInfo.clearValueCount = static_cast<std::uint32_t>(clear.size());
		passInfo.pClearValues = clear.data();
		vkCmdBeginRenderPass(commands, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
		const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_Extent.width),
			static_cast<float>(m_Extent.height), 0.0f, 1.0f };
		const VkRect2D scissor{ { 0, 0 }, m_Extent };
		vkCmdSetViewport(commands, 0, 1, &viewport);
		vkCmdSetScissor(commands, 0, 1, &scissor);
		for (std::size_t index = 0; index < queue.Color.size(); ++index)
		{
			const auto& draw = *queue.Color[index];
			const auto& texture = draw.Texture ? *draw.Texture : white;
			m_LitPipeline->Bind(commands, texture.GetDescriptorSet());
			const auto& mesh = *frame.ColorMeshes.emplace_back(draw.Mesh.GetData());
			const std::array<VkBuffer, 2> buffers{ mesh.Vertices.Get(), frame.MeshInstances->Get() };
			const std::array<VkDeviceSize, 2> offsets{ 0, 0 };
			vkCmdBindVertexBuffers(commands, 0, static_cast<std::uint32_t>(buffers.size()),
				buffers.data(), offsets.data());
			vkCmdBindIndexBuffer(commands, mesh.Indices.Get(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commands, mesh.IndexCount, 1, 0, 0, static_cast<std::uint32_t>(index));
		}
		RecordTexturedBatches(commands, instanceBuffer, m_TexturedPipelines->Opaque, queue.OpaqueBatches);
		RecordTexturedBatches(commands, instanceBuffer, m_TexturedPipelines->Transparent, queue.TransparentBatches);
		vkCmdEndRenderPass(commands);
		if (timestamps)
			vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_Timestamps, firstQuery + 1);
		CheckClearResult(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
	}

	void VulkanClearPass::CollectCompletedResources() noexcept
	{
		for (auto& frame : m_Frames)
			if (frame.InFlight != VK_NULL_HANDLE && vkGetFenceStatus(m_Device, frame.InFlight) == VK_SUCCESS)
			{
				frame.Textures.clear();
				frame.ColorMeshes.clear();
				frame.LitPipeline.reset();
				frame.Textured.reset();
			}
	}

	bool VulkanClearPass::Draw(const VulkanSwapchain& swapchain, VkQueue graphics,
		VkQueue present, const VulkanDrawQueue& queue, const VulkanTexture& white,
		std::span<const std::shared_ptr<const VulkanTexture>> textures, FrameMetrics* metrics)
	{
		auto& frame = m_Frames[m_FrameIndex];
		constexpr auto timeout = std::numeric_limits<std::uint64_t>::max();
		{
			FrameSection wait(metrics ? &metrics->FenceWait : nullptr);
			CheckClearResult(vkWaitForFences(m_Device, 1, &frame.InFlight, VK_TRUE, timeout),
				"vkWaitForFences");
		}
		FrameSection upload(metrics ? &metrics->RecordSubmit : nullptr);
		if (metrics)
		{
			PrepareTimestampQueries();
			ReadTimestamps(frame, *metrics);
		}
		frame.LitPipeline.reset();
		frame.Textured.reset();
		frame.ColorMeshes.clear();
		frame.Textures.assign(textures.begin(), textures.end());
		// Allocate CPU and GPU storage before acquiring the image.
		frame.ColorMeshes.reserve(queue.Color.size());
		UploadInstances(frame.TexturedInstances, std::as_bytes(std::span(queue.TexturedInstances)));
		UploadInstances(frame.MeshInstances, std::as_bytes(std::span(queue.MeshInstances)));
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
		if (imageIndex >= m_Framebuffers.size())
			throw std::runtime_error("Acquired swapchain image index is out of range");

		FrameSection submitting(metrics ? &metrics->RecordSubmit : nullptr);
		const bool timestamps = metrics && m_Timestamps != VK_NULL_HANDLE;
		Record(frame, imageIndex, queue, white, timestamps);
		// Keep the fence signaled on acquisition/recording failure; reset just before submission.
		CheckClearResult(vkResetFences(m_Device, 1, &frame.InFlight), "vkResetFences");
		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submission{};
		submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submission.waitSemaphoreCount = 1;
		submission.pWaitSemaphores = &frame.ImageAvailable;
		submission.pWaitDstStageMask = &waitStage;
		submission.commandBufferCount = 1;
		submission.pCommandBuffers = &frame.Commands;
		submission.signalSemaphoreCount = 1;
		submission.pSignalSemaphores = &m_RenderFinished[imageIndex];
		CheckClearResult(vkQueueSubmit(graphics, 1, &submission, frame.InFlight), "vkQueueSubmit");
		frame.TimestampFrame = timestamps ? metrics->Start : 0;
		frame.LitPipeline = m_LitPipeline;
		frame.Textured = m_TexturedPipelines;

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

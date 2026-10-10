#include "Renderer/Vulkan/VulkanClearPass.hpp"

#include <limits>
#include <stdexcept>
#include <string>

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
	}

	VulkanClearPass::VulkanClearPass(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice, const VulkanSwapchain& swapchain,
		std::uint32_t graphicsFamily,
		const std::filesystem::path& shaderDirectory)
		: m_Device(device.Get()), m_Extent(swapchain.GetExtent())
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
			CheckClearCreation(vkCreateRenderPass(m_Device, &passInfo, nullptr, &m_RenderPass),
				m_RenderPass, "vkCreateRenderPass");
			m_Pipeline.emplace(device, m_RenderPass, shaderDirectory);

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
				framebufferInfo.renderPass = m_RenderPass;
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
		m_Pipeline.reset();
		for (const auto framebuffer : m_Framebuffers)
			if (framebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
		m_DepthAttachments.clear();
		if (m_RenderPass != VK_NULL_HANDLE)
			vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
	}

	void VulkanClearPass::Record(VkCommandBuffer commands, std::uint32_t imageIndex,
		const Mat4& viewProjection, std::span<const VulkanDraw> draws)
	{
		CheckClearResult(vkResetCommandBuffer(commands, 0), "vkResetCommandBuffer");
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CheckClearResult(vkBeginCommandBuffer(commands, &beginInfo), "vkBeginCommandBuffer");

		std::array<VkClearValue, 2> clear{};
		clear[0].color = { { 0.03f, 0.08f, 0.18f, 1.0f } };
		clear[1].depthStencil = { 1.0f, 0 };
		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = m_RenderPass;
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
		for (const auto& draw : draws)
		{
			const ColorPushConstants constants{ viewProjection * draw.Model, draw.Tint };
			m_Pipeline->Bind(commands, constants);
			const VkBuffer vertexBuffer = draw.Mesh.GetVertices().Get();
			const VkDeviceSize vertexOffset = 0;
			vkCmdBindVertexBuffers(commands, 0, 1, &vertexBuffer, &vertexOffset);
			vkCmdBindIndexBuffer(commands, draw.Mesh.GetIndices().Get(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commands, draw.Mesh.GetIndexCount(), 1, 0, 0, 0);
		}
		vkCmdEndRenderPass(commands);
		CheckClearResult(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
	}

	bool VulkanClearPass::Draw(const VulkanSwapchain& swapchain, VkQueue graphics,
		VkQueue present, const Mat4& viewProjection, std::span<const VulkanDraw> draws)
	{
		auto& frame = m_Frames[m_FrameIndex];
		constexpr auto timeout = std::numeric_limits<std::uint64_t>::max();
		CheckClearResult(vkWaitForFences(m_Device, 1, &frame.InFlight, VK_TRUE, timeout),
			"vkWaitForFences");

		std::uint32_t imageIndex = 0;
		const VkResult acquire = vkAcquireNextImageKHR(m_Device, swapchain.Get(), timeout,
			frame.ImageAvailable, VK_NULL_HANDLE, &imageIndex);
		if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
			return false;
		if (acquire != VK_SUBOPTIMAL_KHR)
			CheckClearResult(acquire, "vkAcquireNextImageKHR");
		if (imageIndex >= m_Framebuffers.size())
			throw std::runtime_error("Acquired swapchain image index is out of range");

		Record(frame.Commands, imageIndex, viewProjection, draws);
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

		const VkSwapchainKHR handle = swapchain.Get();
		VkPresentInfoKHR presentation{};
		presentation.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentation.waitSemaphoreCount = 1;
		presentation.pWaitSemaphores = &m_RenderFinished[imageIndex];
		presentation.swapchainCount = 1;
		presentation.pSwapchains = &handle;
		presentation.pImageIndices = &imageIndex;
		const VkResult result = vkQueuePresentKHR(present, &presentation);
		m_FrameIndex = (m_FrameIndex + 1) % m_Frames.size();
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			return false;
		CheckClearResult(result, "vkQueuePresentKHR");
		return acquire != VK_SUBOPTIMAL_KHR;
	}
}

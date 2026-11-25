#include "Renderer/Vulkan/VulkanOverlayPipeline.hpp"

#include "Raven/Renderer/Overlay.hpp"
#include "Renderer/Vulkan/VulkanShaderModule.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace Raven
{
	VulkanOverlayPipeline::VulkanOverlayPipeline(const VulkanDevice& device, VkFormat colorFormat,
		VkFormat depthFormat, VkDescriptorSetLayout bindlessLayout, std::span<const std::uint32_t> vertexWords,
		std::span<const std::uint32_t> fragmentWords)
		: m_Device(device.Get())
	{
		if (m_Device == VK_NULL_HANDLE || colorFormat == VK_FORMAT_UNDEFINED || bindlessLayout == VK_NULL_HANDLE)
			throw std::runtime_error("A device, a color format and the bindless layout are required for the overlay");
		try
		{
			const VulkanShaderModule vertex(device, vertexWords);
			const VulkanShaderModule fragment(device, fragmentWords);
			std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertex.Get();
			stages[0].pName = "VSMain";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragment.Get();
			stages[1].pName = "PSMain";

			const VkVertexInputBindingDescription binding{ 0, sizeof(OverlayQuad), VK_VERTEX_INPUT_RATE_INSTANCE };
			const std::array<VkVertexInputAttributeDescription, 7> attributes
			{
				VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayQuad, Rect) },
				VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayQuad, Uv) },
				VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayQuad, Color) },
				VkVertexInputAttributeDescription{ 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayQuad, BorderColor) },
				VkVertexInputAttributeDescription{ 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayQuad, Clip) },
				VkVertexInputAttributeDescription{ 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayQuad, CornerRadius) },
				VkVertexInputAttributeDescription{ 6, 0, VK_FORMAT_R32G32_UINT, offsetof(OverlayQuad, Texture) }
			};
			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInput.vertexBindingDescriptionCount = 1;
			vertexInput.pVertexBindingDescriptions = &binding;
			vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
			vertexInput.pVertexAttributeDescriptions = attributes.data();
			VkPipelineInputAssemblyStateCreateInfo assembly{};
			assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

			VkPipelineViewportStateCreateInfo viewport{};
			viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewport.viewportCount = 1;
			viewport.scissorCount = 1;
			const std::array<VkDynamicState, 2> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamic{};
			dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
			dynamic.pDynamicStates = dynamicStates.data();

			VkPipelineRasterizationStateCreateInfo raster{};
			raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			raster.polygonMode = VK_POLYGON_MODE_FILL;
			raster.cullMode = VK_CULL_MODE_NONE;
			raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
			raster.lineWidth = 1.0f;
			VkPipelineMultisampleStateCreateInfo multisample{};
			multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			// Drawn over everything in submission order; depth is neither tested nor written.
			VkPipelineDepthStencilStateCreateInfo depth{};
			depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			VkPipelineColorBlendAttachmentState color{};
			color.blendEnable = VK_TRUE;
			color.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			color.colorBlendOp = VK_BLEND_OP_ADD;
			color.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			color.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			color.alphaBlendOp = VK_BLEND_OP_ADD;
			color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments = &color;

			const VkPushConstantRange screen{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2 };
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount = 1;
			layoutInfo.pSetLayouts = &bindlessLayout;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &screen;
			VkResult result = vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_Layout);
			if (result != VK_SUCCESS)
			{
				m_Layout = VK_NULL_HANDLE;
				throw std::runtime_error("vkCreatePipelineLayout failed: " + std::to_string(result));
			}

			VkPipelineRenderingCreateInfoKHR rendering{};
			rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
			rendering.colorAttachmentCount = 1;
			rendering.pColorAttachmentFormats = &colorFormat;
			rendering.depthAttachmentFormat = depthFormat;
			VkGraphicsPipelineCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			info.pNext = &rendering;
			info.stageCount = static_cast<std::uint32_t>(stages.size());
			info.pStages = stages.data();
			info.pVertexInputState = &vertexInput;
			info.pInputAssemblyState = &assembly;
			info.pViewportState = &viewport;
			info.pRasterizationState = &raster;
			info.pMultisampleState = &multisample;
			info.pDepthStencilState = &depth;
			info.pColorBlendState = &blend;
			info.pDynamicState = &dynamic;
			info.layout = m_Layout;
			info.basePipelineIndex = -1;
			result = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &info, nullptr, &m_Pipeline);
			if (result != VK_SUCCESS)
				throw std::runtime_error("vkCreateGraphicsPipelines failed: " + std::to_string(result));
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	VulkanOverlayPipeline::~VulkanOverlayPipeline()
	{
		Destroy();
	}

	void VulkanOverlayPipeline::Record(VkCommandBuffer commands, VkDescriptorSet bindless, VkBuffer buffer,
		VkDeviceSize offset, std::uint32_t count, VkExtent2D extent) const
	{
		if (count == 0)
			return;
		const float size[2]{ static_cast<float>(extent.width), static_cast<float>(extent.height) };
		vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
		vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Layout, 0, 1, &bindless, 0, nullptr);
		vkCmdPushConstants(commands, m_Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(size), size);
		vkCmdBindVertexBuffers(commands, 0, 1, &buffer, &offset);
		vkCmdDraw(commands, 4, count, 0, 0);
	}

	void VulkanOverlayPipeline::Destroy() noexcept
	{
		if (m_Pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
		if (m_Layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(m_Device, m_Layout, nullptr);
		m_Pipeline = VK_NULL_HANDLE;
		m_Layout = VK_NULL_HANDLE;
	}
}

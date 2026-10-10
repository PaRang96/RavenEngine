#include "Renderer/Vulkan/VulkanColorPipeline.hpp"

#include "Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Raven/Renderer/ColorVertex.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace Raven
{
	VulkanColorPipeline::VulkanColorPipeline(const VulkanDevice& device,
		VkRenderPass renderPass, const std::filesystem::path& shaderDirectory)
		: m_Device(device.Get())
	{
		if (m_Device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE)
			throw std::runtime_error("A device and render pass are required for the color pipeline");

		try
		{
			const VulkanShaderModule vertex(device, shaderDirectory / "Color.vert.spv");
			const VulkanShaderModule fragment(device, shaderDirectory / "Color.frag.spv");

			std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertex.Get();
			stages[0].pName = "VSMain";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragment.Get();
			stages[1].pName = "PSMain";

			const VkVertexInputBindingDescription binding
			{
				0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX
			};
			const std::array<VkVertexInputAttributeDescription, 2> attributes
			{
				VkVertexInputAttributeDescription
				{
					0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ColorVertex, Position)
				},
				VkVertexInputAttributeDescription
				{
					1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ColorVertex, Color)
				}
			};
			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInput.vertexBindingDescriptionCount = 1;
			vertexInput.pVertexBindingDescriptions = &binding;
			vertexInput.vertexAttributeDescriptionCount =
				static_cast<std::uint32_t>(attributes.size());
			vertexInput.pVertexAttributeDescriptions = attributes.data();
			VkPipelineInputAssemblyStateCreateInfo assembly{};
			assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo viewport{};
			viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewport.viewportCount = 1;
			viewport.scissorCount = 1;
			const std::array<VkDynamicState, 2> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR };
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
			VkPipelineDepthStencilStateCreateInfo depth{};
			depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depth.depthTestEnable = VK_TRUE;
			depth.depthWriteEnable = VK_TRUE;
			depth.depthCompareOp = VK_COMPARE_OP_LESS;

			VkPipelineColorBlendAttachmentState color{};
			color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments = &color;

			const VkPushConstantRange pushRange
			{
				VK_SHADER_STAGE_VERTEX_BIT,
				0,
				sizeof(ColorPushConstants)
			};
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushRange;
			VkPipelineLayout layout = VK_NULL_HANDLE;
			VkResult result = vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &layout);
			if (result != VK_SUCCESS)
				throw std::runtime_error("vkCreatePipelineLayout failed: " + std::to_string(result));
			m_Layout = layout;

			VkGraphicsPipelineCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
			info.renderPass = renderPass;
			info.subpass = 0;
			info.basePipelineIndex = -1;
			// Pipeline creation can return a valid output even when the call fails.
			// Keep that output owned so the failure path also destroys it.
			result = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &info,
				nullptr, &m_Pipeline);
			if (result != VK_SUCCESS)
				throw std::runtime_error("vkCreateGraphicsPipelines failed: " + std::to_string(result));
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	void VulkanColorPipeline::Bind(VkCommandBuffer commands,
		const ColorPushConstants& constants) const
	{
		vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
		vkCmdPushConstants(commands, m_Layout, VK_SHADER_STAGE_VERTEX_BIT,
			0, sizeof(constants), &constants);
	}

	VulkanColorPipeline::~VulkanColorPipeline()
	{
		Destroy();
	}

	void VulkanColorPipeline::Destroy() noexcept
	{
		if (m_Pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
		if (m_Layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(m_Device, m_Layout, nullptr);
	}
}

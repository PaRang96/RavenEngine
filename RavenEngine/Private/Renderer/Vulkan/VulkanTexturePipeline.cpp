#include "Renderer/Vulkan/VulkanTexturePipeline.hpp"

#include "Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Renderer/Vulkan/VulkanDrawQueue.hpp"
#include "Raven/Renderer/TexturedVertex.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace Raven
{
	VulkanTexturePipeline::VulkanTexturePipeline(const VulkanDevice& device,
		VkRenderPass renderPass, VkDescriptorSetLayout textureLayout, const std::filesystem::path& shaderDirectory,
		TexturePipelineMode mode)
		: m_Device(device.Get())
	{
		if (m_Device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE || textureLayout == VK_NULL_HANDLE)
			throw std::runtime_error("A device, render pass and texture layout are required for the texture pipeline");

		try
		{
			const VulkanShaderModule vertex(device, shaderDirectory / "Texture.vert.spv");
			const VulkanShaderModule fragment(device, shaderDirectory / "Texture.frag.spv");

			std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertex.Get();
			stages[0].pName = "VSMain";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragment.Get();
			stages[1].pName = "PSMain";

			const std::array<VkVertexInputBindingDescription, 2> bindings
			{
				VkVertexInputBindingDescription{ 0, sizeof(TexturedVertex), VK_VERTEX_INPUT_RATE_VERTEX },
				VkVertexInputBindingDescription{ 1, sizeof(TexturedInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE }
			};
			const std::array<VkVertexInputAttributeDescription, 7> attributes
			{
				VkVertexInputAttributeDescription
				{
					0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TexturedVertex, Position)
				},
				VkVertexInputAttributeDescription
				{
					1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TexturedVertex, UV)
				},
				VkVertexInputAttributeDescription{ 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
				VkVertexInputAttributeDescription{ 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
				VkVertexInputAttributeDescription{ 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
				VkVertexInputAttributeDescription{ 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 },
				VkVertexInputAttributeDescription{ 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
					offsetof(TexturedInstanceData, Tint) }
			};
			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
			vertexInput.pVertexBindingDescriptions = bindings.data();
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
			depth.depthWriteEnable = mode == TexturePipelineMode::Opaque ? VK_TRUE : VK_FALSE;
			depth.depthCompareOp = VK_COMPARE_OP_LESS;

			VkPipelineColorBlendAttachmentState color{};
			color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			if (mode == TexturePipelineMode::StraightAlpha)
			{
				color.blendEnable = VK_TRUE;
				color.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				color.colorBlendOp = VK_BLEND_OP_ADD;
				color.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				color.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				color.alphaBlendOp = VK_BLEND_OP_ADD;
			}
			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments = &color;

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount = 1;
			layoutInfo.pSetLayouts = &textureLayout;
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

	void VulkanTexturePipeline::Bind(VkCommandBuffer commands,
		VkDescriptorSet texture) const
	{
		vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
		vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Layout,
			0, 1, &texture, 0, nullptr);
	}

	VulkanTexturePipeline::~VulkanTexturePipeline()
	{
		Destroy();
	}

	void VulkanTexturePipeline::Destroy() noexcept
	{
		if (m_Pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
		if (m_Layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(m_Device, m_Layout, nullptr);
	}
}

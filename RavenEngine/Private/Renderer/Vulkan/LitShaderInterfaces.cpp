#include "Renderer/Vulkan/LitShaderInterfaces.hpp"

#include "Renderer/Vulkan/VulkanFrameData.hpp"
#include "Renderer/Vulkan/VulkanGpuScene.hpp"

#include <cstddef>

namespace Raven
{
	// The frame uniform's members (Raven/Frame.hlsli).
	std::vector<std::uint32_t> FrameOffsets()
	{
		return { offsetof(FrameUniformData, ViewProjection), offsetof(FrameUniformData, View),
			offsetof(FrameUniformData, CameraPosition), offsetof(FrameUniformData, Ambient),
			offsetof(FrameUniformData, LightDirection), offsetof(FrameUniformData, LightColor),
			offsetof(FrameUniformData, Time) };
	}

	// RavenFragmentInput in Raven/Material.hlsli, version 2 (B0 contract 1).
	std::vector<ShaderStageVariable> LitStageVariables()
	{
		return { { 0, 3 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 3 }, { 5, 3 }, { 6, 1, true }, { 7, 1, true }, { 8, 3 } };
	}

	// The fixed interfaces reloaded shaders are checked against (docs/HotReloadDesign.md, H2).
	ShaderInterface LitVertexInterface()
	{
		ShaderInterface result;
		result.Inputs = { { 0, 3 }, { 1, 3 }, { 2, 3 }, { 3, 2 } };
		result.Outputs = LitStageVariables();
		result.Buffers.push_back({ 1, 0, 0, FrameOffsets() });
		// The vertex stage sees the objects, visible slots and materials of the scene set, and a material's
		// vertex offset may read its values (B0 contract 2).
		for (auto& buffer : VulkanGpuScene::GetBufferLayouts(2))
			if (buffer.Binding <= 2 || buffer.Binding == RavenSceneMaterialValuesBinding)
				result.Buffers.push_back(std::move(buffer));
		using Kind = ShaderImageBinding::Kind;
		result.Images = { { 0, 0, Kind::TextureArray }, { 0, 1, Kind::SamplerArray, 2 } };
		return result;
	}

	// Lit fragment shaders (Raven/Material.hlsli): any subset of the inputs, the bindless textures,
	// the sampler set, the frame uniform, and the materials with their values.
	ShaderInterface LitFragmentInterface()
	{
		ShaderInterface result;
		result.Kind = ShaderInterface::Stage::Fragment;
		result.Inputs = LitStageVariables();
		result.Outputs = { { 0, 4 } };
		result.Buffers.push_back({ 1, 0, 0, FrameOffsets() });
		for (auto& buffer : VulkanGpuScene::GetBufferLayouts(2))
			if (buffer.Binding == RavenSceneMaterialsBinding || buffer.Binding == RavenSceneMaterialValuesBinding)
				result.Buffers.push_back(std::move(buffer));
		using Kind = ShaderImageBinding::Kind;
		result.Images = { { 0, 0, Kind::TextureArray }, { 0, 1, Kind::SamplerArray, 2 } };
		return result;
	}
}

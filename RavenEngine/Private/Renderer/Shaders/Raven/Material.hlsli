// Engine interface for lit fragment shaders, version 2 (docs/RenderingContentDesign.md, B0
// contract 1; docs/GpuDrivenDesign.md, G4).
//
// Write lit fragment shaders against these declarations and functions; editing this file
// reloads every watched lit shader. Declaring the same bindings and inputs directly is an
// advanced path whose layout may change between engine versions. A shader may read any subset of
// the inputs and resources.
#ifndef RAVEN_MATERIAL_HLSLI
#define RAVEN_MATERIAL_HLSLI

#include "GpuScene.hlsli"
#include "Raven/Frame.hlsli"

// Bindless textures; element 0 is white.
[[vk::binding(0, 0)]] Texture2D<float4> RavenTextures[];
// The fixed sampler set (FD-21): 0 clamps at the edges, 1 repeats. Both filter linearly between mips.
[[vk::binding(1, 0)]] SamplerState RavenSamplers[2];
[[vk::binding(0, 1)]] ConstantBuffer<FrameData> RavenFrameData;
[[vk::binding(RAVEN_SCENE_MATERIALS, 2)]] StructuredBuffer<MaterialData> RavenMaterials;
[[vk::binding(RAVEN_SCENE_MATERIAL_VALUES, 2)]] StructuredBuffer<float4> RavenMaterialValues;

struct RavenFragmentInput
{
	// Vertex color times the object tint and material base color.
	[[vk::location(0)]] float3 Color : COLOR0;
	[[vk::location(1)]] float2 UV : TEXCOORD0;
	[[vk::location(2)]] float3 Normal : NORMAL;
	// RGB ambient light; W is 1 for lit materials.
	[[vk::location(3)]] nointerpolation float4 AmbientAndLit : TEXCOORD1;
	[[vk::location(4)]] nointerpolation float3 LightDirection : TEXCOORD2;
	[[vk::location(5)]] nointerpolation float3 LightColor : COLOR1;
	// The base-color texture's bindless element (version 1); RavenMaterialTexture reads any slot.
	[[vk::location(6)]] nointerpolation uint Texture : TEXCOORD3;
	// The material's slot in RavenMaterials.
	[[vk::location(7)]] nointerpolation uint Material : TEXCOORD4;
	[[vk::location(8)]] float3 WorldPosition : TEXCOORD5;
};

struct RavenFrame
{
	// Seconds since the renderer started, and since the previous frame.
	float Time;
	float Delta;
	float3 CameraPosition;
};

RavenFrame RavenGetFrame()
{
	RavenFrame frame;
	frame.Time = RavenFrameData.Time.x;
	frame.Delta = RavenFrameData.Time.y;
	frame.CameraPosition = RavenFrameData.CameraPosition.xyz;
	return frame;
}

// One of the material's four texture slots (0 is the base color), sampled with that slot's sampler.
float4 RavenMaterialTexture(RavenFragmentInput input, uint slot, float2 uv)
{
	const MaterialData material = RavenMaterials[input.Material];
	const uint element = material.Textures[slot];
	// A branch rather than an indexed sampler array, which would need another device feature.
	if (material.Samplers[slot] == 1)
		return RavenTextures[NonUniformResourceIndex(element)].Sample(RavenSamplers[1], uv);
	return RavenTextures[NonUniformResourceIndex(element)].Sample(RavenSamplers[0], uv);
}

// One of the values the material file sets for its shader, in declaration order; zero past them.
float4 RavenMaterialValue(RavenFragmentInput input, uint index)
{
	const MaterialData material = RavenMaterials[input.Material];
	return index < material.ValueCount ? RavenMaterialValues[material.ValueOffset + index] : float4(0, 0, 0, 0);
}

// The material's base color: its base-color texture sampled at the UV, times the input color.
float4 RavenSampleBaseColor(RavenFragmentInput input)
{
	const float4 texel = RavenMaterialTexture(input, 0, input.UV);
	return float4(texel.rgb * input.Color, texel.a);
}

// Ambient plus one directional light for lit materials; unlit materials return albedo.
float3 RavenApplyLighting(RavenFragmentInput input, float3 albedo)
{
	if (input.AmbientAndLit.w <= 0.5)
		return albedo;
	const float diffuse = saturate(dot(normalize(input.Normal), -input.LightDirection));
	return albedo * (input.AmbientAndLit.rgb + input.LightColor * diffuse);
}

#endif

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
#include "Raven/Bindless.hlsli"
#include "Raven/Frame.hlsli"

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

// A texture parameter of a material shader (FD-19): the material file names it, and its type says
// whether it repeats or clamps at the edges.
struct RavenClampTexture
{
	uint Element;
	uint3 Padding;
};

struct RavenRepeatTexture
{
	uint Element;
	uint3 Padding;
};

float4 RavenSample(RavenClampTexture parameter, float2 uv)
{
	return RavenTextures[NonUniformResourceIndex(parameter.Element)].Sample(RavenSamplers[0], uv);
}

float4 RavenSample(RavenRepeatTexture parameter, float2 uv)
{
	return RavenTextures[NonUniformResourceIndex(parameter.Element)].Sample(RavenSamplers[1], uv);
}

// A material shader's parameters (FD-19): declare one struct of float4 values and Raven*Texture
// members, at most 16 values and 4 textures, then name it here and read it with RavenGetParameters.
// The material file sets them by member name; the engine reads the names from the compiled shader.
//
//   struct GlowMaterial { RavenClampTexture BaseColor; RavenRepeatTexture Noise; float4 EdgeColor; };
//   RAVEN_MATERIAL_PARAMETERS(GlowMaterial)
//   ... const GlowMaterial parameters = RavenGetParameters(input);
#define RAVEN_MATERIAL_PARAMETERS(Type) \
	[[vk::binding(RAVEN_SCENE_MATERIAL_VALUES, 2)]] StructuredBuffer<Type> RavenParameterBlocks; \
	Type RavenGetParametersOf(uint materialSlot) \
	{ \
		const MaterialData material = RavenMaterials[materialSlot]; \
		return RavenParameterBlocks[material.ValueOffset / max(material.ValueCount, 1u)]; \
	} \
	Type RavenGetParameters(RavenFragmentInput input) \
	{ \
		return RavenGetParametersOf(input.Material); \
	}

// The vertex offset hook (B0 contract 2): a material shader that moves its vertices defines the
// largest distance it moves them, then the function, before or after this include:
//
//   #define RAVEN_OFFSET_BOUND 0.25    // metres; culling grows the object's bounds by it
//   float3 RavenOffset(RavenOffsetInput input) { return float3(0, 0, sin(input.WorldPosition.x) * 0.2); }
//
// The engine runs it after the object transform, in world space, and clamps the result to the bound.
// It may read the material's values with RavenGetParametersOf(input.Material), the time with
// RavenGetFrame(), and textures with SampleLevel (the vertex stage has no implicit mip level). Normals are not re-derived; define RAVEN_OFFSET_NORMAL and
// float3 RavenOffsetNormal(RavenOffsetInput input, float3 normal) to return new ones.
struct RavenOffsetInput
{
	// The vertex in the mesh's own space.
	float3 Position;
	float3 Normal;
	// After the object transform, before the offset.
	float3 WorldPosition;
	float3 WorldNormal;
	float2 UV;
	// Vertex color times the object tint and material base color.
	float3 Color;
	uint Material;
};

// One 16-byte member of the material's parameter block, in declaration order; zero past them.
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

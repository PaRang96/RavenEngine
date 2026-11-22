// Engine interface for lit fragment shaders (docs/GpuDrivenDesign.md, G4).
//
// Write lit fragment shaders against these declarations and functions; editing this file
// reloads every watched lit shader. Declaring the same bindings and inputs directly is an
// advanced path whose layout may change between engine versions.
#ifndef RAVEN_MATERIAL_HLSLI
#define RAVEN_MATERIAL_HLSLI

// Bindless base-color textures; element 0 is white.
[[vk::binding(0, 0)]] Texture2D<float4> RavenTextures[];
[[vk::binding(1, 0)]] SamplerState RavenSampler;

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
	[[vk::location(6)]] nointerpolation uint Texture : TEXCOORD3;
};

// The material's base color: its texture sampled at the UV, times the input color.
float4 RavenSampleBaseColor(RavenFragmentInput input)
{
	const float4 texel = RavenTextures[NonUniformResourceIndex(input.Texture)].Sample(RavenSampler, input.UV);
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

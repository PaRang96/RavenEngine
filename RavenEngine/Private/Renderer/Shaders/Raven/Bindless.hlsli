// The bindless texture array and the fixed sampler set (docs/GpuDrivenDesign.md, G4; FD-21), shared
// by the lit, sprite and overlay programs (docs/RenderingContentDesign.md, B0 contract 4).
#ifndef RAVEN_BINDLESS_HLSLI
#define RAVEN_BINDLESS_HLSLI

// Bindless textures; element 0 is white.
[[vk::binding(0, 0)]] Texture2D<float4> RavenTextures[];
// The fixed sampler set (FD-21): 0 clamps at the edges, 1 repeats. Both filter linearly between mips.
[[vk::binding(1, 0)]] SamplerState RavenSamplers[2];

// One element of the array, sampled with the clamping sampler.
float4 RavenSampleClamped(uint element, float2 uv)
{
	return RavenTextures[NonUniformResourceIndex(element)].Sample(RavenSamplers[0], uv);
}

#endif

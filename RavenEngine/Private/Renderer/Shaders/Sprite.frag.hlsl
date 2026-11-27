// Sprites (docs/RenderingContentDesign.md, B0 contract 4): the texture times the tint. One program
// serves the three sprite pipelines; the cut-out pipeline sets RavenSpriteCutout, which discards texels
// below alpha 0.5, and the opaque and blended pipelines keep early depth testing without it.

#include "Raven/Bindless.hlsli"

[[vk::constant_id(0)]] const bool RavenSpriteCutout = false;

struct FragmentInput
{
	[[vk::location(0)]] float2 UV : TEXCOORD0;
	[[vk::location(1)]] nointerpolation float4 Tint : COLOR0;
	[[vk::location(2)]] nointerpolation uint Texture : TEXCOORD1;
};

float4 PSMain(FragmentInput input) : SV_Target0
{
	const float4 color = RavenSampleClamped(input.Texture, input.UV) * input.Tint;
	if (RavenSpriteCutout && color.a < 0.5)
		discard;
	return color;
}

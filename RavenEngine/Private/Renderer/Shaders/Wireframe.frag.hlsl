#include "Raven/Material.hlsli"

// Debug view: edges drawn as lines over the shaded image (docs/HotReloadDesign.md, H3).
// Dark lines on light materials and light lines on dark ones keep every edge visible.
float4 PSMain(RavenFragmentInput input) : SV_Target0
{
	const float luminance = dot(RavenSampleBaseColor(input).rgb, float3(0.299, 0.587, 0.114));
	return float4((luminance > 0.5 ? 0.05 : 0.95).xxx, 1.0);
}

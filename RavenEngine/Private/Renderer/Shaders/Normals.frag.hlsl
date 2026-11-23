#include "Raven/Material.hlsli"

// Debug view: the world-space normal mapped from [-1, 1] to [0, 1] (docs/HotReloadDesign.md, H3).
float4 PSMain(RavenFragmentInput input) : SV_Target0
{
	return float4(normalize(input.Normal) * 0.5 + 0.5, 1.0);
}

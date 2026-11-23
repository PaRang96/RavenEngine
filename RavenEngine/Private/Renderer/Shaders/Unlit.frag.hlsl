#include "Raven/Material.hlsli"

// Debug view: base color only, without lighting (docs/HotReloadDesign.md, H3).
float4 PSMain(RavenFragmentInput input) : SV_Target0
{
	return float4(RavenSampleBaseColor(input).rgb, 1.0);
}

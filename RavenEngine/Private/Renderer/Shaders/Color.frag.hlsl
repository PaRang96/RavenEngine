#include "Raven/Material.hlsli"

float4 PSMain(RavenFragmentInput input) : SV_Target0
{
	const float3 albedo = RavenSampleBaseColor(input).rgb;
	return float4(RavenApplyLighting(input, albedo), 1.0);
}

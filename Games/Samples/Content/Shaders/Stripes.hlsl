// A sample material shader (docs/RenderingContentDesign.md, FD-19): world-space stripes over a
// tiling pattern. The material file sets the parameters below by name.
#include "Raven/Material.hlsli"

struct StripesMaterial
{
	// Tiles across the surface, so it repeats at the edges.
	RavenRepeatTexture Pattern;
	// RGB is the stripe color; A is how strongly it covers the surface.
	float4 StripeColor;
	// X: the width of one stripe, in metres.
	float4 Width;
};
RAVEN_MATERIAL_PARAMETERS(StripesMaterial)

float4 PSMain(RavenFragmentInput input) : SV_Target0
{
	const StripesMaterial parameters = RavenGetParameters(input);
	const float width = max(parameters.Width.x, 0.01);
	const float band = frac((input.WorldPosition.x + input.WorldPosition.y) / (2.0 * width));
	float3 albedo = input.Color * RavenSample(parameters.Pattern, input.UV * 2.0).rgb;
	if (band >= 0.5)
		albedo = lerp(albedo, parameters.StripeColor.rgb, parameters.StripeColor.a);
	return float4(RavenApplyLighting(input, albedo), 1.0);
}

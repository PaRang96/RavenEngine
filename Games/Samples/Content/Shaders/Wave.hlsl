// A sample material shader with a vertex offset (docs/RenderingContentDesign.md, B0 contract 2): the
// surface rises and falls in waves. The offset depends only on the vertex's position, so the copies a
// mesh keeps of one point at its seams move together and the surface never cracks; an offset along
// normals would split it wherever normals are split.
#define RAVEN_OFFSET_BOUND 0.2
#include "Raven/Material.hlsli"

struct WaveMaterial
{
	// X: height in metres (at most the bound); Y: speed; Z: waves per metre.
	float4 Wave;
	// RGB multiplies the surface color.
	float4 Tint;
};
RAVEN_MATERIAL_PARAMETERS(WaveMaterial)

float3 RavenOffset(RavenOffsetInput input)
{
	const WaveMaterial parameters = RavenGetParametersOf(input.Material);
	const float phase = RavenGetFrame().Time * parameters.Wave.y +
		dot(input.WorldPosition, float3(1.0, 1.0, 0.5)) * parameters.Wave.z;
	return float3(0.0, 0.0, sin(phase) * parameters.Wave.x);
}

float4 PSMain(RavenFragmentInput input) : SV_Target0
{
	const WaveMaterial parameters = RavenGetParameters(input);
	return float4(RavenApplyLighting(input, input.Color * parameters.Tint.rgb), 1.0);
}

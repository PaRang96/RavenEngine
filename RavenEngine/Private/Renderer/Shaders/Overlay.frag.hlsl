// The shared GPU 2D path (docs/UIDesign.md, section 4): rounded rectangles with borders and soft
// edges, textured images, and MTSDF glyphs, all from one instance format.
[[vk::binding(0, 0)]] Texture2D<float4> RavenTextures[];
[[vk::binding(1, 0)]] SamplerState RavenSampler;

static const uint FlagImage = 1;
static const uint FlagGlyph = 2;

struct QuadOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float2 Local : TEXCOORD0;
	[[vk::location(1)]] nointerpolation float2 HalfSize : TEXCOORD1;
	[[vk::location(2)]] float2 Uv : TEXCOORD2;
	[[vk::location(3)]] nointerpolation float4 Color : COLOR0;
	[[vk::location(4)]] nointerpolation float4 BorderColor : COLOR1;
	[[vk::location(5)]] nointerpolation float4 Clip : CLIP;
	[[vk::location(6)]] nointerpolation float4 Shape : SHAPE;
	[[vk::location(7)]] nointerpolation uint2 Info : INFO;
};

float Median(float3 value)
{
	return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}

float4 PSMain(QuadOutput input) : SV_Target0
{
	const float2 pixel = input.Position.xy;
	if (pixel.x < input.Clip.x || pixel.y < input.Clip.y || pixel.x >= input.Clip.z || pixel.y >= input.Clip.w)
		discard;

	const uint flags = input.Info.y;
	if (flags == FlagGlyph)
	{
		// MTSDF: the median of RGB is the sharp-corner distance; the range maps it to screen pixels.
		const float4 field = RavenTextures[NonUniformResourceIndex(input.Info.x)].Sample(RavenSampler, input.Uv);
		const float distance = input.Shape.w * (Median(field.rgb) - 0.5);
		const float coverage = saturate(distance + 0.5);
		return float4(input.Color.rgb, input.Color.a * coverage);
	}

	float4 fill = input.Color;
	if (flags == FlagImage)
		fill *= RavenTextures[NonUniformResourceIndex(input.Info.x)].Sample(RavenSampler, input.Uv);

	// Signed distance to the rounded rectangle: negative inside.
	const float radius = min(input.Shape.x, min(input.HalfSize.x, input.HalfSize.y));
	const float2 q = abs(input.Local) - input.HalfSize + radius;
	const float edge = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
	const float softness = max(input.Shape.z, 1.0);
	const float coverage = saturate(0.5 - edge / softness);
	const float border = input.Shape.y;
	if (border > 0.0)
	{
		const float inside = saturate(0.5 - (edge + border));
		fill = lerp(input.BorderColor, fill, inside);
	}
	return float4(fill.rgb, fill.a * coverage);
}

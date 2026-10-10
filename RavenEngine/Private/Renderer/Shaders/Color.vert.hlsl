struct ColorPushConstants
{
	// Explicit columns match Mat4 bytes without relying on HLSL matrix packing defaults.
	[[vk::offset(0)]] float4 Column0;
	[[vk::offset(16)]] float4 Column1;
	[[vk::offset(32)]] float4 Column2;
	[[vk::offset(48)]] float4 Column3;
	[[vk::offset(64)]] float4 Tint;
};

[[vk::push_constant]] ColorPushConstants Draw;

struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float3 Color : COLOR0;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float3 Color : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;
	output.Position = Draw.Column0 * input.Position.x + Draw.Column1 * input.Position.y
		+ Draw.Column2 * input.Position.z + Draw.Column3;
	output.Color = input.Color * Draw.Tint.rgb;
	return output;
}

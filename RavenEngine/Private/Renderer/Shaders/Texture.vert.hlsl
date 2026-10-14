struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float2 UV : TEXCOORD0;
	[[vk::location(2)]] float4 Column0 : TEXCOORD1;
	[[vk::location(3)]] float4 Column1 : TEXCOORD2;
	[[vk::location(4)]] float4 Column2 : TEXCOORD3;
	[[vk::location(5)]] float4 Column3 : TEXCOORD4;
	[[vk::location(6)]] float4 Tint : COLOR0;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float2 UV : TEXCOORD0;
	[[vk::location(1)]] float4 Tint : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;
	output.Position = input.Column0 * input.Position.x + input.Column1 * input.Position.y
		+ input.Column2 * input.Position.z + input.Column3;
	output.UV = input.UV;
	output.Tint = input.Tint;
	return output;
}

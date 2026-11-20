struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float3 Color : COLOR0;
	[[vk::location(2)]] float3 Normal : NORMAL;
	[[vk::location(3)]] float2 UV : TEXCOORD0;
	[[vk::location(4)]] float4 Clip0 : TEXCOORD1;
	[[vk::location(5)]] float4 Clip1 : TEXCOORD2;
	[[vk::location(6)]] float4 Clip2 : TEXCOORD3;
	[[vk::location(7)]] float4 Clip3 : TEXCOORD4;
	[[vk::location(8)]] float4 Normal0 : TEXCOORD5;
	[[vk::location(9)]] float4 Normal1 : TEXCOORD6;
	[[vk::location(10)]] float4 Normal2 : TEXCOORD7;
	[[vk::location(11)]] float4 Tint : COLOR1;
	[[vk::location(12)]] float4 AmbientAndLit : TEXCOORD8;
	[[vk::location(13)]] float4 LightDirection : TEXCOORD9;
	[[vk::location(14)]] float4 LightColor : COLOR2;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float3 Color : COLOR0;
	[[vk::location(1)]] float2 UV : TEXCOORD0;
	[[vk::location(2)]] float3 Normal : NORMAL;
	[[vk::location(3)]] nointerpolation float4 AmbientAndLit : TEXCOORD1;
	[[vk::location(4)]] nointerpolation float3 LightDirection : TEXCOORD2;
	[[vk::location(5)]] nointerpolation float3 LightColor : COLOR1;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;
	output.Position = input.Clip0 * input.Position.x + input.Clip1 * input.Position.y
		+ input.Clip2 * input.Position.z + input.Clip3;
	output.Color = input.Color * input.Tint.rgb;
	output.UV = input.UV;
	output.Normal = input.Normal0.xyz * input.Normal.x + input.Normal1.xyz * input.Normal.y
		+ input.Normal2.xyz * input.Normal.z;
	output.AmbientAndLit = input.AmbientAndLit;
	output.LightDirection = input.LightDirection.xyz;
	output.LightColor = input.LightColor.xyz;
	return output;
}

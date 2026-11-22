// Written once per frame (docs/RendererDesign.md, R4). Only this stage reads it, so the
// reloadable fragment shader keeps its interface: the texture at set 0 and the inputs below.
struct FrameData
{
	float4 ViewProjection[4];
	float4 View[4];
	float4 CameraPosition;
	float4 Ambient;
	float4 LightDirection;
	float4 LightColor;
};

[[vk::binding(0, 1)]] ConstantBuffer<FrameData> Frame;

struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float3 Color : COLOR0;
	[[vk::location(2)]] float3 Normal : NORMAL;
	[[vk::location(3)]] float2 UV : TEXCOORD0;
	[[vk::location(4)]] float4 World0 : TEXCOORD1;
	[[vk::location(5)]] float4 World1 : TEXCOORD2;
	[[vk::location(6)]] float4 World2 : TEXCOORD3;
	// Normal0.w holds the lit flag.
	[[vk::location(7)]] float4 Normal0 : TEXCOORD4;
	[[vk::location(8)]] float4 Normal1 : TEXCOORD5;
	[[vk::location(9)]] float4 Normal2 : TEXCOORD6;
	[[vk::location(10)]] float4 Tint : COLOR1;
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
	const float4 local = float4(input.Position, 1.0);
	const float3 world = float3(dot(input.World0, local), dot(input.World1, local), dot(input.World2, local));
	output.Position = Frame.ViewProjection[0] * world.x + Frame.ViewProjection[1] * world.y
		+ Frame.ViewProjection[2] * world.z + Frame.ViewProjection[3];
	output.Color = input.Color * input.Tint.rgb;
	output.UV = input.UV;
	output.Normal = input.Normal0.xyz * input.Normal.x + input.Normal1.xyz * input.Normal.y
		+ input.Normal2.xyz * input.Normal.z;
	output.AmbientAndLit = float4(Frame.Ambient.rgb, input.Normal0.w);
	output.LightDirection = Frame.LightDirection.xyz;
	output.LightColor = Frame.LightColor.rgb;
	return output;
}

#include "GpuScene.hlsli"

#include "Raven/Frame.hlsli"

[[vk::binding(0, 1)]] ConstantBuffer<FrameData> Frame;
// GPU-driven objects (docs/GpuDrivenDesign.md, G5): culling wrote this draw's visible slots.
[[vk::binding(RAVEN_SCENE_OBJECTS, 2)]] StructuredBuffer<ObjectData> Objects;
[[vk::binding(RAVEN_SCENE_VISIBLE, 2)]] StructuredBuffer<uint> Visible;
[[vk::binding(RAVEN_SCENE_MATERIALS, 2)]] StructuredBuffer<MaterialData> Materials;

struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float3 Color : COLOR0;
	[[vk::location(2)]] float3 Normal : NORMAL;
	[[vk::location(3)]] float2 UV : TEXCOORD0;
};

// Matches RavenFragmentInput in Raven/Material.hlsli.
struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float3 Color : COLOR0;
	[[vk::location(1)]] float2 UV : TEXCOORD0;
	[[vk::location(2)]] float3 Normal : NORMAL;
	[[vk::location(3)]] nointerpolation float4 AmbientAndLit : TEXCOORD1;
	[[vk::location(4)]] nointerpolation float3 LightDirection : TEXCOORD2;
	[[vk::location(5)]] nointerpolation float3 LightColor : COLOR1;
	[[vk::location(6)]] nointerpolation uint Texture : TEXCOORD3;
	[[vk::location(7)]] nointerpolation uint Material : TEXCOORD4;
	[[vk::location(8)]] float3 WorldPosition : TEXCOORD5;
};

// SV_InstanceID is the Vulkan instance index, which includes the command's firstInstance.
VertexOutput VSMain(VertexInput input, uint instance : SV_InstanceID)
{
	const ObjectData object = Objects[Visible[instance]];
	const MaterialData material = Materials[object.Material];
	VertexOutput output;
	const float4 local = float4(input.Position, 1.0);
	const float3 world = float3(dot(object.World[0], local), dot(object.World[1], local), dot(object.World[2], local));
	output.Position = Frame.ViewProjection[0] * world.x + Frame.ViewProjection[1] * world.y
		+ Frame.ViewProjection[2] * world.z + Frame.ViewProjection[3];
	output.Color = input.Color * object.Tint.rgb * material.BaseColor.rgb;
	output.UV = input.UV;
	output.Normal = object.Normal[0].xyz * input.Normal.x + object.Normal[1].xyz * input.Normal.y
		+ object.Normal[2].xyz * input.Normal.z;
	output.AmbientAndLit = float4(Frame.Ambient.rgb, (material.Flags & 1u) != 0 ? 1.0 : 0.0);
	output.LightDirection = Frame.LightDirection.xyz;
	output.LightColor = Frame.LightColor.rgb;
	output.Texture = material.Textures.x;
	output.Material = object.Material;
	output.WorldPosition = world;
	return output;
}

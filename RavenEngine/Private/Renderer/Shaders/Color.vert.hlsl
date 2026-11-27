// The engine's lit vertex program (docs/GpuDrivenDesign.md, G5). A material shader that defines
// RAVEN_OFFSET_BOUND also gets a vertex program compiled from its own source followed by this file,
// which moves each vertex by the material's RavenOffset (docs/RenderingContentDesign.md, B0 contract 2).

#include "Raven/Material.hlsli"

// GPU-driven objects (docs/GpuDrivenDesign.md, G5): culling wrote this draw's visible slots.
[[vk::binding(RAVEN_SCENE_OBJECTS, 2)]] StructuredBuffer<ObjectData> Objects;
[[vk::binding(RAVEN_SCENE_VISIBLE, 2)]] StructuredBuffer<uint> Visible;

#ifdef RAVEN_OFFSET_BOUND
// The declared bound, kept in the compiled program as specialization constant 1 so the engine reads
// it for culling; the offset is clamped to it, so a vertex never leaves the bounds culling assumes.
[[vk::constant_id(1)]] const float RavenOffsetBound = RAVEN_OFFSET_BOUND;
#endif

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
	const MaterialData material = RavenMaterials[object.Material];
	VertexOutput output;
	const float4 local = float4(input.Position, 1.0);
	float3 world = float3(dot(object.World[0], local), dot(object.World[1], local), dot(object.World[2], local));
	output.Normal = object.Normal[0].xyz * input.Normal.x + object.Normal[1].xyz * input.Normal.y
		+ object.Normal[2].xyz * input.Normal.z;
	output.Color = input.Color * object.Tint.rgb * material.BaseColor.rgb;
#ifdef RAVEN_OFFSET_BOUND
	RavenOffsetInput hook;
	hook.Position = input.Position;
	hook.Normal = input.Normal;
	hook.WorldPosition = world;
	hook.WorldNormal = output.Normal;
	hook.UV = input.UV;
	hook.Color = output.Color;
	hook.Material = object.Material;
	float3 offset = RavenOffset(hook);
	const float length2 = dot(offset, offset);
	if (length2 > RavenOffsetBound * RavenOffsetBound)
		offset *= RavenOffsetBound * rsqrt(length2);
	world += offset;
#ifdef RAVEN_OFFSET_NORMAL
	// Normals are not re-derived from the offset; a shader that needs them returns its own.
	output.Normal = RavenOffsetNormal(hook, output.Normal);
#endif
#endif
	output.Position = RavenFrameData.ViewProjection[0] * world.x + RavenFrameData.ViewProjection[1] * world.y
		+ RavenFrameData.ViewProjection[2] * world.z + RavenFrameData.ViewProjection[3];
	output.UV = input.UV;
	output.AmbientAndLit = float4(RavenFrameData.Ambient.rgb, (material.Flags & 1u) != 0 ? 1.0 : 0.0);
	output.LightDirection = RavenFrameData.LightDirection.xyz;
	output.LightColor = RavenFrameData.LightColor.rgb;
	output.Texture = material.Textures.x;
	output.Material = object.Material;
	output.WorldPosition = world;
	return output;
}

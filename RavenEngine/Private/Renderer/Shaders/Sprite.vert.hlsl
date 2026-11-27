// Sprites on the GPU path (docs/RenderingContentDesign.md, B0 contract 4): render-world objects
// drawn on the shared quad. Opaque and cut-out sprites come from culling, blended sprites from the
// CPU's back-to-front list; both name object slots in Visible.

#include "Raven/Material.hlsli"

[[vk::binding(RAVEN_SCENE_OBJECTS, 2)]] StructuredBuffer<ObjectData> Objects;
[[vk::binding(RAVEN_SCENE_VISIBLE, 2)]] StructuredBuffer<uint> Visible;

// The quad of the mesh pool; its normal is not read.
struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float3 Color : COLOR0;
	[[vk::location(3)]] float2 UV : TEXCOORD0;
};

// Matches Sprite.frag.hlsl.
struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float2 UV : TEXCOORD0;
	[[vk::location(1)]] nointerpolation float4 Tint : COLOR0;
	[[vk::location(2)]] nointerpolation uint Texture : TEXCOORD1;
};

// SV_InstanceID is the Vulkan instance index, which includes the draw's firstInstance.
VertexOutput VSMain(VertexInput input, uint instance : SV_InstanceID)
{
	const ObjectData object = Objects[Visible[instance]];
	const MaterialData material = RavenMaterials[object.Material];
	VertexOutput output;
	// The world rows include the sprite's size and pivot; a sprite is unlit, so its first normal row
	// carries the texture rectangle (u, v, width, height) instead.
	const float4 local = float4(input.Position, 1.0);
	const float3 world = float3(dot(object.World[0], local), dot(object.World[1], local), dot(object.World[2], local));
	output.Position = RavenFrameData.ViewProjection[0] * world.x + RavenFrameData.ViewProjection[1] * world.y
		+ RavenFrameData.ViewProjection[2] * world.z + RavenFrameData.ViewProjection[3];
	const float4 rect = object.Normal[0];
	output.UV = rect.xy + input.UV * rect.zw;
	output.Tint = object.Tint * material.BaseColor * float4(input.Color, 1.0);
	output.Texture = material.Textures.x;
	return output;
}

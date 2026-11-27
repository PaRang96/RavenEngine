#include "GpuScene.hlsli"

// Frustum culling into per-group indirect commands (docs/GpuDrivenDesign.md, G5;
// docs/RenderingContentDesign.md, B0 contract 3).
[[vk::binding(RAVEN_SCENE_OBJECTS, 0)]] StructuredBuffer<ObjectData> Objects;
[[vk::binding(RAVEN_SCENE_VISIBLE, 0)]] RWStructuredBuffer<uint> Visible;
[[vk::binding(RAVEN_SCENE_MESHES, 0)]] StructuredBuffer<MeshData> Meshes;
[[vk::binding(RAVEN_SCENE_COMMANDS, 0)]] RWStructuredBuffer<DrawCommand> Commands;

struct CullConstants
{
	// Normalized planes of the Vulkan clip volume; see Renderer/Visibility.hpp.
	float4 Planes[6];
	uint ObjectCount;
	uint3 Padding;
};

[[vk::push_constant]] CullConstants Cull;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint slot = id.x;
	if (slot >= Cull.ObjectCount)
		return;
	const ObjectData object = Objects[slot];
	if ((object.Flags & 1u) != 0)
		return;
	const MeshData mesh = Meshes[object.Group];
	if (mesh.IndexCount == 0)
		return;
	const float4 local = float4(mesh.Center.xyz, 1.0);
	const float3 center = float3(dot(object.World[0], local), dot(object.World[1], local), dot(object.World[2], local));
	// Center.w is the world-space bound of a material's vertex offset (B0 contract 2).
	const float3 extent = float3(dot(abs(object.World[0].xyz), mesh.HalfExtent.xyz),
		dot(abs(object.World[1].xyz), mesh.HalfExtent.xyz), dot(abs(object.World[2].xyz), mesh.HalfExtent.xyz)) +
		mesh.Center.w;
	[unroll]
	for (uint plane = 0; plane < 6; ++plane)
	{
		const float4 p = Cull.Planes[plane];
		const float distance = dot(p.xyz, center) + p.w;
		const float radius = dot(abs(p.xyz), extent);
		// Keep touching bounds, with the CPU path's relative tolerance.
		const float tolerance = 1.0e-4 * max(1.0, max(abs(distance), radius));
		if (distance + radius < -tolerance)
			return;
	}
	uint index;
	InterlockedAdd(Commands[mesh.CommandIndex].InstanceCount, 1u, index);
	Visible[Commands[mesh.CommandIndex].FirstInstance + index] = slot;
}

// GPU-resident scene records (docs/GpuDrivenDesign.md, G2/G3/G5). They mirror
// Renderer/RenderWorldState.hpp and Renderer/Vulkan/VulkanGpuScene.hpp; std430 layout.
#ifndef RAVEN_GPU_SCENE_HLSLI
#define RAVEN_GPU_SCENE_HLSLI

struct ObjectData
{
	float4 World[3];
	float4 Normal[3];
	float4 Tint;
	uint Group;
	uint Material;
	// Bit 0: hidden.
	uint Flags;
	uint Padding;
};

struct MaterialData
{
	float4 BaseColor;
	// Bindless elements; x is the base color.
	uint4 Textures;
	uint4 Samplers;
	// Bit 0: lit.
	uint Flags;
	uint ValueOffset;
	uint ValueCount;
	uint Padding;
};

struct MeshData
{
	uint IndexCount;
	uint FirstIndex;
	int VertexOffset;
	// The group's indirect command; commands are contiguous per pipeline.
	uint CommandIndex;
	float4 Center;
	float4 HalfExtent;
};

// VkDrawIndexedIndirectCommand.
struct DrawCommand
{
	uint IndexCount;
	uint InstanceCount;
	uint FirstIndex;
	int VertexOffset;
	uint FirstInstance;
};

// One set shared by the culling passes (set 0) and the lit pipeline (set 2).
#define RAVEN_SCENE_OBJECTS 0
#define RAVEN_SCENE_VISIBLE 1
#define RAVEN_SCENE_MATERIALS 2
#define RAVEN_SCENE_MESHES 3
#define RAVEN_SCENE_COMMANDS 4
#define RAVEN_SCENE_OBJECT_BYTES 5
#define RAVEN_SCENE_UPLOAD 6
// The materials' float4 values, which ValueOffset and ValueCount index (B0 contract 1).
#define RAVEN_SCENE_MATERIAL_VALUES 7

#endif

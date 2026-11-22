#include "GpuScene.hlsli"

// Writes changed object records from the frame's upload ring into the resident object
// buffer (docs/GpuDrivenDesign.md, G2): one thread per 16-byte quarter of a 128-byte record.
[[vk::binding(RAVEN_SCENE_OBJECT_BYTES, 0)]] RWByteAddressBuffer ObjectBytes;
[[vk::binding(RAVEN_SCENE_UPLOAD, 0)]] ByteAddressBuffer Upload;

struct ScatterConstants
{
	// Byte offsets in the upload ring: one uint slot per record, then the 128-byte records.
	uint SlotOffset;
	uint RecordOffset;
	uint RecordCount;
	uint Padding;
};

[[vk::push_constant]] ScatterConstants Scatter;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint record = id.x >> 3;
	const uint quarter = id.x & 7u;
	if (record >= Scatter.RecordCount)
		return;
	const uint slot = Upload.Load(Scatter.SlotOffset + record * 4u);
	const uint4 value = Upload.Load4(Scatter.RecordOffset + record * 128u + quarter * 16u);
	ObjectBytes.Store4(slot * 128u + quarter * 16u, value);
}

// Developer debug lines, such as the collider view (docs/GameServicesDesign.md, FD-32).
// Only the frame uniform's view-projection is read; the rest of FrameData keeps its layout.
#include "Raven/Frame.hlsli"

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> Frame;

struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	// DebugLineVertex::Color, unpacked from RGBA8.
	[[vk::location(1)]] float4 Color : COLOR0;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float4 Color : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;
	output.Position = Frame.ViewProjection[0] * input.Position.x + Frame.ViewProjection[1] * input.Position.y
		+ Frame.ViewProjection[2] * input.Position.z + Frame.ViewProjection[3];
	// Lines on a surface, such as a collider matching its mesh, would z-fight with it: pull them a
	// little toward the camera. Line primitives get no rasterizer depth bias.
	output.Position.z -= 0.0002 * output.Position.w;
	output.Color = input.Color;
	return output;
}

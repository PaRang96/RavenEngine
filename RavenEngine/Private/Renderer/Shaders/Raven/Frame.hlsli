// The frame uniform at set 1 binding 0 (docs/RendererDesign.md, R4), written once per frame. It
// mirrors FrameUniformData in Renderer/Vulkan/VulkanFrameData.hpp; every shader that reads it
// declares it through this file, because the interface check compares its whole layout.
#ifndef RAVEN_FRAME_HLSLI
#define RAVEN_FRAME_HLSLI

struct FrameData
{
	float4 ViewProjection[4];
	float4 View[4];
	float4 CameraPosition;
	float4 Ambient;
	float4 LightDirection;
	float4 LightColor;
	// X: seconds since the renderer started; Y: seconds since the previous frame.
	float4 Time;
};

#endif

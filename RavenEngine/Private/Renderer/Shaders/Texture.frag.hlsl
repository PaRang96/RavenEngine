[[vk::binding(0, 0)]] Texture2D<float4> SurfaceTexture;
[[vk::binding(1, 0)]] SamplerState SurfaceSampler;

struct FragmentInput
{
	[[vk::location(0)]] float2 UV : TEXCOORD0;
	[[vk::location(1)]] float4 Tint : COLOR0;
};

float4 PSMain(FragmentInput input) : SV_Target0
{
	return SurfaceTexture.Sample(SurfaceSampler, input.UV) * input.Tint;
}

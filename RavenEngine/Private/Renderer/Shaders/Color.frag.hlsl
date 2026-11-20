[[vk::binding(0, 0)]] Texture2D<float4> BaseColorTexture;
[[vk::binding(1, 0)]] SamplerState BaseColorSampler;

struct FragmentInput
{
	[[vk::location(0)]] float3 Color : COLOR0;
	[[vk::location(1)]] float2 UV : TEXCOORD0;
	[[vk::location(2)]] float3 Normal : NORMAL;
	[[vk::location(3)]] nointerpolation float4 AmbientAndLit : TEXCOORD1;
	[[vk::location(4)]] nointerpolation float3 LightDirection : TEXCOORD2;
	[[vk::location(5)]] nointerpolation float3 LightColor : COLOR1;
};

float4 PSMain(FragmentInput input) : SV_Target0
{
	const float3 albedo = BaseColorTexture.Sample(BaseColorSampler, input.UV).rgb * input.Color;
	float3 illumination = float3(1.0, 1.0, 1.0);
	if (input.AmbientAndLit.w > 0.5)
	{
		const float diffuse = saturate(dot(normalize(input.Normal), -input.LightDirection));
		illumination = input.AmbientAndLit.rgb + input.LightColor * diffuse;
	}
	return float4(albedo * illumination, 1.0);
}

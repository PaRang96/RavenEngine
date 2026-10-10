struct FragmentInput
{
	[[vk::location(0)]] float3 Color : COLOR0;
};

float4 PSMain(FragmentInput input) : SV_Target0
{
	return float4(input.Color, 1.0f);
}

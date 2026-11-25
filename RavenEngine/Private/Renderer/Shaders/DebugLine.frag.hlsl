// Developer debug lines (docs/GameServicesDesign.md, FD-32): the vertex color, unlit.
float4 PSMain([[vk::location(0)]] float4 color : COLOR0) : SV_Target0
{
	return color;
}

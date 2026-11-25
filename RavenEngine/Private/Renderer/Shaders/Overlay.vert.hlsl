// The shared GPU 2D path (docs/UIDesign.md, section 4): one instanced quad per OverlayQuad, drawn as
// a four-vertex strip in framebuffer pixels.
struct Viewport
{
	float2 Size;
};
[[vk::push_constant]] Viewport Screen;

struct QuadInput
{
	[[vk::location(0)]] float4 Rect : RECT;
	[[vk::location(1)]] float4 Uv : TEXCOORD0;
	[[vk::location(2)]] float4 Color : COLOR0;
	[[vk::location(3)]] float4 BorderColor : COLOR1;
	[[vk::location(4)]] float4 Clip : CLIP;
	// Corner radius, border width, softness, distance range.
	[[vk::location(5)]] float4 Shape : SHAPE;
	// Texture element, flags.
	[[vk::location(6)]] uint2 Info : INFO;
};

struct QuadOutput
{
	float4 Position : SV_Position;
	// Pixels from the quad's center, and its half size.
	[[vk::location(0)]] float2 Local : TEXCOORD0;
	[[vk::location(1)]] nointerpolation float2 HalfSize : TEXCOORD1;
	[[vk::location(2)]] float2 Uv : TEXCOORD2;
	[[vk::location(3)]] nointerpolation float4 Color : COLOR0;
	[[vk::location(4)]] nointerpolation float4 BorderColor : COLOR1;
	[[vk::location(5)]] nointerpolation float4 Clip : CLIP;
	[[vk::location(6)]] nointerpolation float4 Shape : SHAPE;
	[[vk::location(7)]] nointerpolation uint2 Info : INFO;
};

QuadOutput VSMain(QuadInput input, uint vertex : SV_VertexID)
{
	const float2 corner = float2(vertex & 1, vertex >> 1);
	const float2 pixel = input.Rect.xy + corner * input.Rect.zw;
	QuadOutput output;
	output.Position = float4(pixel / Screen.Size * 2.0 - 1.0, 0.0, 1.0);
	output.HalfSize = input.Rect.zw * 0.5;
	output.Local = (corner - 0.5) * input.Rect.zw;
	output.Uv = lerp(input.Uv.xy, input.Uv.zw, corner);
	output.Color = input.Color;
	output.BorderColor = input.BorderColor;
	output.Clip = input.Clip;
	output.Shape = input.Shape;
	output.Info = input.Info;
	return output;
}

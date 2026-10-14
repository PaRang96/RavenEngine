#include "OrthographicSample.hpp"

#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <cmath>

namespace Raven
{
	OrthographicSample::OrthographicSample(VulkanContext& renderer)
		: m_Assets(renderer),
		  m_Quad(renderer.CreateTexturedMesh(std::array<TexturedVertex, 4>
		  {
			TexturedVertex{ { -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, 0.5f }, { 1.0f, 0.0f } },
			TexturedVertex{ { -0.5f, 0.0f, 0.5f }, { 0.0f, 0.0f } }
		  }, std::array<std::uint16_t, 6>{ 0, 1, 2, 2, 3, 0 })),
		  m_Texture(m_Assets.LoadTexture(SampleAssets::GetUVGrid())),
		  m_AlternateTexture(m_Assets.LoadTexture(SampleAssets::GetUVGridAlternate())),
		  m_OpaqueDraws{ VulkanTexturedDraw{ m_Quad, m_Texture },
			  VulkanTexturedDraw{ m_Quad, m_AlternateTexture }, VulkanTexturedDraw{ m_Quad, m_Texture } },
		  m_TransparentDraws{ VulkanTransparentDraw{ { m_Quad, m_Texture } },
			  VulkanTransparentDraw{ { m_Quad, m_AlternateTexture } } }
	{
		// Equal world sizes at different depths must have equal screen sizes.
		m_OpaqueDraws[0].Model = Translation({ -2.0f, -2.0f, 1.0f }) * Scale({ 1.5f, 1.0f, 1.5f });
		m_OpaqueDraws[1].Model = Translation({ 2.0f, 2.0f, 1.0f }) * Scale({ 1.5f, 1.0f, 1.5f });
		// Pan right to bring the initially offscreen sprite into the same frustum path.
		m_OpaqueDraws[2].Model = Translation({ 9.0f, 0.0f, 1.0f }) * Scale({ 1.5f, 1.0f, 1.5f });
		m_OpaqueDraws[2].Tint = { 0.5f, 1.0f, 0.5f, 1.0f };
		m_TransparentDraws[0].Draw.Model = Translation({ -0.4f, -0.3f, -1.0f }) * Scale({ 2.2f, 1.0f, 2.2f });
		m_TransparentDraws[0].Draw.Tint = { 1.0f, 0.55f, 0.55f, 0.5f };
		m_TransparentDraws[1].Draw.Model = Translation({ 0.4f, 0.3f, -1.0f }) * Scale({ 2.2f, 1.0f, 2.2f });
		m_TransparentDraws[1].Draw.Tint = { 0.55f, 0.75f, 1.0f, 0.65f };
	}

	void OrthographicSample::OnFrame(VulkanContext& renderer,
		const InputState& input, const FrameTime& frameTime)
	{
		Update(input, frameTime);
		renderer.DrawFrame(m_Camera, {}, m_OpaqueDraws, m_TransparentDraws);
	}

	void OrthographicSample::Update(const InputState& input, const FrameTime& frameTime)
	{
		if (input.WasKeyPressed(Key::Home))
			m_Camera = OrthographicCamera{};
		if (input.WasKeyPressed(Key::F1))
		{
			auto& order = m_TransparentDraws[1].SortOrder;
			order = order == 0 ? 1 : 0;
			Log(LogLevel::Info, order == 0 ? "2D transparent order: automatic camera depth" :
				"2D transparent order: blue-tinted sprite composited last");
		}

		const float delta = static_cast<float>(std::min(frameTime.DeltaSeconds, 0.1));
		const auto step = [&input, delta](Key key)
		{
			return (input.WasKeyPressed(key) ? 0.05f : 0.0f) + (input.IsKeyDown(key) ? delta : 0.0f);
		};
		const float zoom = input.ScrollY * 0.12f + step(Key::PageUp) - step(Key::PageDown);
		m_Camera.VerticalSize = std::clamp(m_Camera.VerticalSize * std::exp(-zoom), 1.0f, 24.0f);
		const float panSpeed = m_Camera.VerticalSize * 0.6f;
		const Vec3 movement{ (step(Key::Right) - step(Key::Left)) * panSpeed, 0.0f,
			(step(Key::Up) - step(Key::Down)) * panSpeed };
		m_Camera.Position = m_Camera.Position + movement;
		m_Camera.Target = m_Camera.Target + movement;
	}
}

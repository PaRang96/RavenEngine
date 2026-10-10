#include "PerspectiveSample.hpp"

#include "Raven/Core/FrameTime.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <cmath>

namespace Raven
{
	namespace
	{
		// Face shades make the unlit geometry readable without introducing materials yet.
		constexpr std::array<ColorVertex, 24> CubeVertices
		{
			ColorVertex{ { -0.5f, -0.5f, -0.5f }, { 0.85f, 0.85f, 0.85f } },
			ColorVertex{ { 0.5f, -0.5f, -0.5f }, { 0.85f, 0.85f, 0.85f } },
			ColorVertex{ { 0.5f, -0.5f, 0.5f }, { 0.85f, 0.85f, 0.85f } },
			ColorVertex{ { -0.5f, -0.5f, 0.5f }, { 0.85f, 0.85f, 0.85f } },
			ColorVertex{ { 0.5f, 0.5f, -0.5f }, { 0.55f, 0.55f, 0.55f } },
			ColorVertex{ { -0.5f, 0.5f, -0.5f }, { 0.55f, 0.55f, 0.55f } },
			ColorVertex{ { -0.5f, 0.5f, 0.5f }, { 0.55f, 0.55f, 0.55f } },
			ColorVertex{ { 0.5f, 0.5f, 0.5f }, { 0.55f, 0.55f, 0.55f } },
			ColorVertex{ { -0.5f, 0.5f, -0.5f }, { 0.65f, 0.65f, 0.65f } },
			ColorVertex{ { -0.5f, -0.5f, -0.5f }, { 0.65f, 0.65f, 0.65f } },
			ColorVertex{ { -0.5f, -0.5f, 0.5f }, { 0.65f, 0.65f, 0.65f } },
			ColorVertex{ { -0.5f, 0.5f, 0.5f }, { 0.65f, 0.65f, 0.65f } },
			ColorVertex{ { 0.5f, -0.5f, -0.5f }, { 0.75f, 0.75f, 0.75f } },
			ColorVertex{ { 0.5f, 0.5f, -0.5f }, { 0.75f, 0.75f, 0.75f } },
			ColorVertex{ { 0.5f, 0.5f, 0.5f }, { 0.75f, 0.75f, 0.75f } },
			ColorVertex{ { 0.5f, -0.5f, 0.5f }, { 0.75f, 0.75f, 0.75f } },
			ColorVertex{ { -0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f } },
			ColorVertex{ { 0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f } },
			ColorVertex{ { 0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f } },
			ColorVertex{ { -0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f } },
			ColorVertex{ { -0.5f, 0.5f, -0.5f }, { 0.35f, 0.35f, 0.35f } },
			ColorVertex{ { 0.5f, 0.5f, -0.5f }, { 0.35f, 0.35f, 0.35f } },
			ColorVertex{ { 0.5f, -0.5f, -0.5f }, { 0.35f, 0.35f, 0.35f } },
			ColorVertex{ { -0.5f, -0.5f, -0.5f }, { 0.35f, 0.35f, 0.35f } }
		};
		constexpr std::array<std::uint16_t, 36> CubeIndices
		{
			0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4,
			8, 9, 10, 10, 11, 8, 12, 13, 14, 14, 15, 12,
			16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20
		};
	}

	PerspectiveSample::PerspectiveSample(VulkanContext& renderer)
		: m_Cube(renderer.CreateMesh(CubeVertices, CubeIndices)),
		  m_Draws{ VulkanDraw{ m_Cube }, VulkanDraw{ m_Cube }, VulkanDraw{ m_Cube } }
	{
		ResetCamera();
		UpdateDraws();
	}

	void PerspectiveSample::OnFrame(VulkanContext& renderer,
		const InputState& input, const FrameTime& frameTime)
	{
		Update(input, frameTime);
		renderer.DrawFrame(m_Camera, m_Draws);
	}

	void PerspectiveSample::ResetCamera()
	{
		m_Yaw = -std::numbers::pi_v<float> / 2.0f;
		m_Pitch = 0.4f;
		m_Distance = 7.5f;
		m_Camera.Target = { 0.0f, 0.3f, 0.2f };
		m_Camera.Position = m_Camera.Target + Vec3
		{
			m_Distance * std::cos(m_Pitch) * std::cos(m_Yaw),
			m_Distance * std::cos(m_Pitch) * std::sin(m_Yaw),
			m_Distance * std::sin(m_Pitch)
		};
	}

	void PerspectiveSample::Update(const InputState& input, const FrameTime& frameTime)
	{
		if (input.WasKeyPressed(Key::R) || input.WasKeyPressed(Key::Home))
			ResetCamera();
		if (input.WasKeyPressed(Key::Space))
			m_Animate = !m_Animate;

		const float delta = static_cast<float>(std::min(frameTime.DeltaSeconds, 0.1));
		const auto orbitStep = [&input, delta](Key key)
		{
			// A short tap survives a press/release in the same event poll.
			return (input.WasKeyPressed(key) ? 0.1f : 0.0f) +
				(input.IsKeyDown(key) ? delta : 0.0f);
		};
		m_Yaw += orbitStep(Key::Right) - orbitStep(Key::Left);
		m_Yaw = std::remainder(m_Yaw, 2.0f * std::numbers::pi_v<float>);
		m_Pitch = std::clamp(m_Pitch + orbitStep(Key::Up) - orbitStep(Key::Down), -1.3f, 1.3f);
		m_Distance = std::clamp(m_Distance - input.ScrollY * 0.5f, 2.5f, 25.0f);
		m_Camera.Position = m_Camera.Target + Vec3
		{
			m_Distance * std::cos(m_Pitch) * std::cos(m_Yaw),
			m_Distance * std::cos(m_Pitch) * std::sin(m_Yaw),
			m_Distance * std::sin(m_Pitch)
		};

		if (m_Animate)
			m_Angle = std::fmod(m_Angle + frameTime.DeltaSeconds * 0.7,
				2.0 * std::numbers::pi_v<double>);
		UpdateDraws();
	}

	void PerspectiveSample::UpdateDraws()
	{
		const float angle = static_cast<float>(m_Angle);
		// Submit the near cube first and the far cube afterward to exercise depth rejection.
		m_Draws[0].Model = Translation({ -0.35f, -0.55f, 0.6f }) *
			RotationZ(angle) * RotationX(angle * 0.37f) * Scale({ 1.2f, 1.2f, 1.2f });
		m_Draws[0].Tint = { 1.0f, 0.45f, 0.12f, 1.0f };
		m_Draws[1].Model = Translation({ 0.45f, 1.0f, 0.5f }) *
			RotationZ(-angle * 0.65f) * RotationX(-0.25f) * Scale({ 1.0f, 1.0f, 1.0f });
		m_Draws[1].Tint = { 0.12f, 0.8f, 0.9f, 1.0f };
		m_Draws[2].Model = Translation({ 0.0f, 0.5f, -0.65f }) * Scale({ 8.0f, 8.0f, 0.1f });
		m_Draws[2].Tint = { 0.22f, 0.27f, 0.34f, 1.0f };
	}
}

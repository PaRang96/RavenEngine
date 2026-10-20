#include "PerspectiveSample.hpp"

#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/ECS/RenderComponents.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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

		// An XZ quad facing the initial camera. Images are top-row first; V=0 is the top edge.
		constexpr std::array<TexturedVertex, 4> QuadVertices
		{
			TexturedVertex{ { -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, 0.5f }, { 1.0f, 0.0f } },
			TexturedVertex{ { -0.5f, 0.0f, 0.5f }, { 0.0f, 0.0f } }
		};
		constexpr std::array<std::uint16_t, 6> QuadIndices{ 0, 1, 2, 2, 3, 0 };

	}

	PerspectiveSample::PerspectiveSample(VulkanContext& renderer, GameInstance& instance)
		: m_Renderer(renderer),
		  m_Game(instance),
		  m_Assets(renderer),
		  m_Cube(renderer.CreateMeshHandle(CubeVertices, CubeIndices)),
		  m_Quad(renderer.CreateTexturedMesh(QuadVertices, QuadIndices)),
		  m_Texture(m_Assets.LoadTexture(SampleAssets::GetUVGrid())),
		  m_AlternateTexture(m_Assets.LoadTexture(SampleAssets::GetUVGridAlternate())),
		  m_TexturedDraws{ VulkanTexturedDraw{ m_Quad, m_Texture },
			  VulkanTexturedDraw{ m_Quad, m_AlternateTexture } },
		  m_TransparentDraws{ VulkanTransparentDraw{ { m_Quad, m_Texture } },
			  VulkanTransparentDraw{ { m_Quad, m_AlternateTexture } } }
	{
		m_TexturedDraws[0].Model = Translation({ -2.0f, 0.5f, 0.6f }) * Scale({ 1.6f, 1.0f, 1.6f });
		m_TexturedDraws[1].Model = Translation({ 2.0f, 0.5f, 0.6f }) * Scale({ 1.6f, 1.0f, 1.6f });
		m_TexturedDraws[1].Tint = { 0.8f, 1.0f, 0.8f, 1.0f };
		// Overlap in front of the cubes. Orbit changes their relative camera depth.
		m_TransparentDraws[0].Draw.Model = Translation({ -0.45f, -1.55f, 0.65f }) * Scale({ 1.8f, 1.0f, 1.8f });
		m_TransparentDraws[0].Draw.Tint = { 1.0f, 0.65f, 0.65f, 0.45f };
		m_TransparentDraws[1].Draw.Model = Translation({ 0.45f, -1.15f, 0.85f }) * Scale({ 1.8f, 1.0f, 1.8f });
		m_TransparentDraws[1].Draw.Tint = { 0.65f, 0.8f, 1.0f, 0.65f };
		std::array<ECS::TransformComponent, 3> transforms{};
		transforms[0].Position = { -0.35f, -0.55f, 0.6f };
		transforms[0].Scale = { 1.2f, 1.2f, 1.2f };
		transforms[1].Position = { 0.45f, 1.0f, 0.5f };
		transforms[1].Rotation = { -0.25f, 0.0f, 0.0f };
		transforms[2].Position = { 0.0f, 0.5f, -0.65f };
		transforms[2].Scale = { 8.0f, 8.0f, 0.1f };
		const std::array<Vec4, 3> tints
		{
			Vec4{ 1.0f, 0.45f, 0.12f, 1.0f },
			Vec4{ 0.12f, 0.8f, 0.9f, 1.0f },
			Vec4{ 0.22f, 0.27f, 0.34f, 1.0f }
		};
		auto& registry = m_Game.GetRegistry();
		try
		{
			m_Draws.reserve(m_MeshEntities.size());
			for (std::size_t index = 0; index < m_MeshEntities.size(); ++index)
			{
				auto& entity = m_MeshEntities[index];
				entity = registry.Create();
				ECS::StaticMeshComponent mesh;
				mesh.Mesh = m_Cube;
				mesh.Tint = tints[index];
				if (!registry.Add(entity, transforms[index]) ||
					!registry.Add(entity, mesh))
					throw std::logic_error("Cannot initialize static mesh entity");
			}
			ResetCamera();
			UpdateDraws();
		}
		catch (...)
		{
			for (const auto entity : m_MeshEntities) registry.Destroy(entity);
			throw;
		}
	}

	PerspectiveSample::~PerspectiveSample()
	{
		for (const auto entity : m_MeshEntities) m_Game.GetRegistry().Destroy(entity);
	}

	void PerspectiveSample::OnFrame(VulkanContext& renderer,
		const InputState& input, const FrameTime& frameTime)
	{
		Update(input, frameTime);
		renderer.DrawFrame(m_Camera, m_Draws, m_TexturedDraws, m_TransparentDraws);
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
		if (input.WasKeyPressed(Key::O) || input.WasKeyPressed(Key::F1))
		{
			auto& order = m_TransparentDraws[1].SortOrder;
			order = order == 0 ? 1 : 0;
			Log(LogLevel::Info, order == 0 ? "Transparent order: automatic camera depth" :
				"Transparent order: blue-tinted quad composited last");
		}

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
		auto& registry = m_Game.GetRegistry();
		ECS::TransformComponent transform;
		if (registry.Read(m_MeshEntities[0], transform))
		{
			transform.Rotation = { angle * 0.37f, 0.0f, angle };
			registry.Write(m_MeshEntities[0], transform);
		}
		if (registry.Read(m_MeshEntities[1], transform))
		{
			transform.Rotation = { -0.25f, 0.0f, -angle * 0.65f };
			registry.Write(m_MeshEntities[1], transform);
		}

		m_Draws.clear();
		for (const auto entity : registry.GetEntities())
		{
			ECS::StaticMeshComponent mesh;
			if (!registry.Read(entity, transform) || !registry.Read(entity, mesh))
				continue;
			if (const auto* resource = m_Renderer.ResolveMesh(mesh.Mesh))
				m_Draws.push_back({ *resource, transform.GetMatrix(), mesh.Tint });
		}
	}
}

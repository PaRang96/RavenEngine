#include "OrthographicSample.hpp"

#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Game/GameComponents.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace Raven
{
	namespace
	{
		void ResetOrthographicCamera(ECS::TransformComponent& transform, ECS::CameraComponent& camera)
		{
			transform = ECS::TransformComponent{};
			transform.Position = { 0.0f, -1.0f, 0.0f };
			camera = ECS::CameraComponent{};
			camera.ProjectionMode = ECS::CameraProjectionMode::Orthographic;
		}
	}

	OrthographicSample::OrthographicSample(VulkanContext& renderer, GameInstance& instance)
		: m_Game(instance),
		  m_Assets(renderer),
		  m_Quad(renderer.CreateTexturedMesh(std::array<TexturedVertex, 4>
		  {
			TexturedVertex{ { -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, 0.5f }, { 1.0f, 0.0f } },
			TexturedVertex{ { -0.5f, 0.0f, 0.5f }, { 0.0f, 0.0f } }
		  }, std::array<std::uint16_t, 6>{ 0, 1, 2, 2, 3, 0 }))
	{
		// Equal world sizes at different depths must have equal screen sizes.
		const std::array<Vec3, 5> positions
		{
			Vec3{ -2.0f, -2.0f, 1.0f }, Vec3{ 2.0f, 2.0f, 1.0f },
			// Pan right to bring this initially offscreen sprite into the frustum.
			Vec3{ 9.0f, 0.0f, 1.0f }, Vec3{ -0.4f, -0.3f, -1.0f }, Vec3{ 0.4f, 0.3f, -1.0f }
		};
		const std::array<Vec4, 5> tints
		{
			Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f },
			Vec4{ 0.5f, 1.0f, 0.5f, 1.0f }, Vec4{ 1.0f, 0.55f, 0.55f, 0.5f },
			Vec4{ 0.55f, 0.75f, 1.0f, 0.65f }
		};
		auto& registry = instance.GetRegistry();
		try
		{
			m_OpaqueDraws.reserve(m_SpriteEntities.size());
			m_TransparentDraws.reserve(m_SpriteEntities.size());
			for (std::size_t index = 0; index < m_SpriteEntities.size(); ++index)
			{
				auto& entity = m_SpriteEntities[index];
				entity = registry.Create();
				ECS::TransformComponent transform;
				transform.Position = positions[index];
				transform.Scale = index < 3 ? Vec3{ 1.5f, 1.0f, 1.5f } : Vec3{ 2.2f, 1.0f, 2.2f };
				ECS::SpriteComponent sprite;
				sprite.Texture = index == 1 || index == 4 ? SampleAssets::GetUVGridAlternate() : SampleAssets::GetUVGrid();
				sprite.Tint = tints[index];
				sprite.BlendMode = index < 3 ? ECS::SpriteBlendMode::Opaque : ECS::SpriteBlendMode::Alpha;
				if (!registry.Add(entity, transform) || !registry.Add(entity, sprite))
					throw std::logic_error("Cannot initialize sprite entity");
			}
			
			// Preserve the existing DLL until its planned migration to Transform.
			SampleGame::Position position;
			position.Location = positions.front();
			m_PreviousGamePosition = position.Location;
			SampleGame::Velocity velocity;
			velocity.Linear = { 0.75f, 0.0f, 0.0f };
			
			if (!registry.Add(m_SpriteEntities.front(), position) || !registry.Add(m_SpriteEntities.front(), velocity))
				throw std::logic_error("Cannot initialize the moving sprite");
			
			m_CameraEntity = registry.Create();
			ECS::TransformComponent cameraTransform;
			ECS::CameraComponent camera;
			ResetOrthographicCamera(cameraTransform, camera);
			
			if (!registry.Add(m_CameraEntity, cameraTransform) || !registry.Add(m_CameraEntity, camera))
				throw std::logic_error("Cannot initialize the orthographic camera");

			UpdateDraws();
		}
		catch (...)
		{
			registry.Destroy(m_CameraEntity);
			for (const auto entity : m_SpriteEntities) registry.Destroy(entity);
			throw;
		}
	}

	OrthographicSample::~OrthographicSample()
	{
		auto& registry = m_Game.GetRegistry();
		registry.Destroy(m_CameraEntity);
		for (const auto entity : m_SpriteEntities)
			registry.Destroy(entity);
	}

	void OrthographicSample::OnFrame(VulkanContext& renderer,
		const InputState& input, const FrameTime& frameTime)
	{
		Update(input, frameTime);
		SyncLegacyMovement();
		UpdateDraws();
		
		renderer.DrawFrame([this](std::uint32_t width, std::uint32_t height)
		-> std::optional<CameraView>
		{
			const auto skipView = [this](std::string_view reason) -> std::optional<CameraView>
			{
				if (!m_CameraViewUnavailable)
					Log(LogLevel::Warning, reason);
				m_CameraViewUnavailable = true;
				return std::nullopt;
			};

			const auto& registry = m_Game.GetRegistry();
			ECS::TransformComponent transform;
			ECS::CameraComponent camera;

			if (!registry.Read(m_CameraEntity, transform) || !registry.Read(m_CameraEntity, camera))
				return skipView("Orthographic camera is missing or has been destroyed");
			
			try
			{
				const float aspect = static_cast<float>(width) / static_cast<float>(height);
				const CameraView view = ECS::GetCameraView(transform, camera, aspect);
				m_CameraViewUnavailable = false;
				return view;
			}
			catch (const std::invalid_argument& error)
			{
				return skipView(error.what());
			}
		}, {}, m_OpaqueDraws, m_TransparentDraws);
	}

	void OrthographicSample::Update(const InputState& input, const FrameTime& frameTime)
	{
		auto& registry = m_Game.GetRegistry();
		if (input.WasKeyPressed(Key::F1))
		{
			ECS::SpriteComponent sprite;
			if (registry.Read(m_SpriteEntities.back(), sprite))
			{
				sprite.SortOrder = sprite.SortOrder == 0 ? 1 : 0;
				registry.Write(m_SpriteEntities.back(), sprite);
				Log(LogLevel::Info, sprite.SortOrder == 0 ? "2D transparent order: automatic camera depth" :
					"2D transparent order: blue-tinted sprite composited last");
			}
		}

		const float delta = static_cast<float>(std::min(frameTime.DeltaSeconds, 0.1));
		const auto step = [&input, delta](Key key)
		{
			return (input.WasKeyPressed(key) ? 0.05f : 0.0f) + (input.IsKeyDown(key) ? delta : 0.0f);
		};
		
		const bool reset = input.WasKeyPressed(Key::Home);
		const float zoom = input.ScrollY * 0.12f + step(Key::PageUp) - step(Key::PageDown);
		const float horizontal = step(Key::Right) - step(Key::Left);
		const float vertical = step(Key::Up) - step(Key::Down);
		const bool pan = horizontal != 0.0f || vertical != 0.0f;
		if (!reset && !pan && zoom == 0.0f)
			return;

		ECS::TransformComponent transform;
		ECS::CameraComponent camera;
		if (!registry.Read(m_CameraEntity, transform) || !registry.Read(m_CameraEntity, camera))
			return;

		if (reset)
			ResetOrthographicCamera(transform, camera);

		if (!std::isfinite(camera.OrthographicHeight) || camera.OrthographicHeight <= 0.0f)
			return;

		if (zoom != 0.0f)
			camera.OrthographicHeight = std::clamp(camera.OrthographicHeight * std::exp(-zoom), 1.0f, 24.0f);

		if (pan)
		{
			const float panSpeed = camera.OrthographicHeight * 0.6f;
			transform.Position = transform.Position + Vec3{ horizontal * panSpeed, 0.0f, vertical * panSpeed };
		}

		if ((reset || pan) && !registry.Write(m_CameraEntity, transform))
			Log(LogLevel::Warning, "Cannot update orthographic camera transform");
		if ((reset || zoom != 0.0f) && !registry.Write(m_CameraEntity, camera))
			Log(LogLevel::Warning, "Cannot update orthographic camera projection");
 	}

	void OrthographicSample::SyncLegacyMovement()
	{
		auto& registry = m_Game.GetRegistry();
		SampleGame::Position position;
		if (!registry.Read(m_SpriteEntities.front(), position))
			return;
		const Vec3 movement = position.Location - m_PreviousGamePosition;
		m_PreviousGamePosition = position.Location;
		ECS::TransformComponent transform;
		if ((movement.X != 0.0f || movement.Y != 0.0f || movement.Z != 0.0f) &&
			registry.Read(m_SpriteEntities.front(), transform))
		{
			transform.Position = transform.Position + movement;
			registry.Write(m_SpriteEntities.front(), transform);
		}
	}

	void OrthographicSample::UpdateDraws()
	{
		m_OpaqueDraws.clear();
		m_TransparentDraws.clear();
		auto& registry = m_Game.GetRegistry();
		for (const auto entity : registry.GetEntities())
		{
			ECS::TransformComponent transform;
			ECS::SpriteComponent sprite;
			if (!registry.Read(entity, transform) || !registry.Read(entity, sprite) || !sprite.Texture.IsValid())
				continue;
			if (sprite.BlendMode != ECS::SpriteBlendMode::Opaque && sprite.BlendMode != ECS::SpriteBlendMode::Alpha)
				continue;
			const VulkanTexturedDraw draw{ m_Quad, m_Assets.LoadTexture(sprite.Texture), transform.GetMatrix(), sprite.Tint };
			if (sprite.BlendMode == ECS::SpriteBlendMode::Opaque)
				m_OpaqueDraws.push_back(draw);
			else
				m_TransparentDraws.push_back({ draw, sprite.SortLayer, sprite.SortOrder });
		}
	}
}

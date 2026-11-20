#include "OrthographicSample.hpp"
#include "OrthographicLevel.hpp"
#include "SampleCamera.hpp"

#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{

	OrthographicSample::OrthographicSample(VulkanContext& renderer, GameInstance& instance, bool watchWall, bool watchLevel)
		: m_Game(instance),
		  m_Assets(renderer, watchLevel, false, watchWall),
		  m_Audio(m_Assets.GetCatalog()),
		  m_Quad(renderer.CreateTexturedMesh(std::array<TexturedVertex, 4>
		  {
			TexturedVertex{ { -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, 0.5f }, { 1.0f, 0.0f } },
			TexturedVertex{ { -0.5f, 0.0f, 0.5f }, { 0.0f, 0.0f } }
		  }, std::array<std::uint16_t, 6>{ 0, 1, 2, 2, 3, 0 }))
	{
		m_Session = std::make_unique<SceneSession>(m_Assets.GetProject(), SceneType::Orthographic,
			[this](std::span<const std::byte> bytes, const std::filesystem::path& path, const AssetCatalog& catalog)
			{
				return std::make_unique<OrthographicLevel>(m_Game.GetRegistry(), m_Assets.GetProject(), bytes, path, catalog);
			});
		UpdateDraws();
	}

	OrthographicSample::~OrthographicSample() = default;

	GameBindings OrthographicSample::GetGameBindings() const noexcept
	{
		if (GetLevel().GetAttemptResult() == OrthographicLevel::AttemptResult::Playing)
			return m_Session->GetActive().GetBindings();
		return {};
	}

	OrthographicLevel& OrthographicSample::GetLevel() const noexcept
	{
		return static_cast<OrthographicLevel&>(m_Session->GetActive());
	}

	bool OrthographicSample::PrepareFrame(const InputState& input, const FrameTime& frameTime)
	{
		if (m_Session->Update(input.WasKeyPressed(Key::F3), input.WasKeyPressed(Key::F4)))
		{
			m_Audio.Stop();
			m_CameraViewUnavailable = false;
			return true;
		}
		Update(input, frameTime);
		return false;
	}

	void OrthographicSample::OnGameplayStep(const GameUpdateResult& result)
	{
		const auto outcome = GetLevel().Step(result.PlayerDisplacement);
		if (outcome == OrthographicLevel::AttemptResult::Won)
			m_Audio.Play(GetLevel().GetWinSound());
		else if (outcome == OrthographicLevel::AttemptResult::Lost)
			m_Audio.Play(GetLevel().GetLossSound());
	}

	void OrthographicSample::OnFrame(VulkanContext& renderer, const InputState&, const FrameTime&)
	{
		const auto frameStart = std::chrono::steady_clock::now();
		if (m_Assets.ApplyReloads().Texture && !GetLevel().GetPlayer().IsNull())
			m_WallUpload = std::chrono::steady_clock::now() - frameStart;
		UpdateDraws();

		const auto presented = renderer.DrawFrame([this](std::uint32_t width, std::uint32_t height)
		{
			return PrepareSampleCameraView(m_Game.GetRegistry(), GetLevel().GetSelectedCamera().Entity, width, height,
				m_CameraViewUnavailable, "Orthographic camera is missing or has been destroyed");
		}, {}, m_OpaqueDraws, m_TransparentDraws);
		if (presented && m_WallUpload && !GetLevel().GetPlayer().IsNull())
		{
			const auto presentTime = std::chrono::steady_clock::now().time_since_epoch();
			const auto frameWork = presentTime - frameStart.time_since_epoch();
			Log(LogLevel::Info, std::format("Courtyard wall reload presented: upload {:.3f} ms, frame {:.3f} ms, clock {} ns",
				m_WallUpload->count(), std::chrono::duration<double, std::milli>(frameWork).count(),
				std::chrono::duration_cast<std::chrono::nanoseconds>(presentTime).count()));
			m_WallUpload.reset();
		}
	}

	void OrthographicSample::Update(const InputState& input, const FrameTime& frameTime)
	{
		auto& registry = m_Game.GetRegistry();
		if (input.WasKeyPressed(Key::F1))
		{
			ECS::SpriteComponent sprite;
			if (registry.Read(GetLevel().GetOrderSprite(), sprite))
			{
				sprite.SortOrder = sprite.SortOrder == 0 ? 1 : 0;
				registry.Write(GetLevel().GetOrderSprite(), sprite);
				Log(LogLevel::Info, sprite.SortOrder == 0 ? "2D transparent order: automatic camera depth" :
					"2D transparent order: bound sprite order override");
			}
		}

		if (input.WasKeyPressed(Key::F2))
		{
			GetLevel().SwitchCamera();
			m_CameraViewUnavailable = false;
		}

		const float delta = static_cast<float>(std::min(frameTime.DeltaSeconds, 0.1));

		const bool reset = input.WasKeyPressed(Key::Home);
		const float zoom = input.ScrollY * 0.12f + CameraKeyStep(input, Key::PageUp, 0.05f, delta) -
			CameraKeyStep(input, Key::PageDown, 0.05f, delta);
		const float horizontal = CameraKeyStep(input, Key::Right, 0.05f, delta) -
			CameraKeyStep(input, Key::Left, 0.05f, delta);
		const float vertical = CameraKeyStep(input, Key::Up, 0.05f, delta) -
			CameraKeyStep(input, Key::Down, 0.05f, delta);
		const bool pan = horizontal != 0.0f || vertical != 0.0f;
		if (!reset && !pan && zoom == 0.0f)
			return;

		const auto& selected = GetLevel().GetSelectedCamera();
		ECS::TransformComponent transform;
		ECS::CameraComponent camera;
		if (!registry.Read(selected.Entity, transform) || !registry.Read(selected.Entity, camera))
			return;

		if (reset)
		{
			transform = selected.Transform;
			camera = selected.Lens;
		}

		if (!std::isfinite(camera.OrthographicHeight) || camera.OrthographicHeight <= 0.0f)
			return;

		if (zoom != 0.0f)
			camera.OrthographicHeight = std::clamp(camera.OrthographicHeight * std::exp(-zoom), 1.0f, 24.0f);

		if (pan)
		{
			const float panSpeed = camera.OrthographicHeight * 0.6f;
			transform.Position = transform.Position + Vec3{ horizontal * panSpeed, 0.0f, vertical * panSpeed };
		}

		if ((reset || pan) && !registry.Write(selected.Entity, transform))
			Log(LogLevel::Warning, "Cannot update orthographic camera transform");
		if ((reset || zoom != 0.0f) && !registry.Write(selected.Entity, camera))
			Log(LogLevel::Warning, "Cannot update orthographic camera projection");
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
			Vec4 tint = sprite.Tint;
			if (entity == GetLevel().GetPlayer())
			{
				switch (GetLevel().GetAttemptResult())
				{
				case OrthographicLevel::AttemptResult::Playing:
					break;
				case OrthographicLevel::AttemptResult::Won:
					tint = { 0.2f, 1.0f, 0.25f, tint.W };
					break;
				case OrthographicLevel::AttemptResult::Lost:
					tint = { 1.0f, 0.2f, 0.2f, tint.W };
					break;
				}
			}
			const VulkanTexturedDraw draw{ m_Quad, GetLevel().UseAsset(m_Assets.GetCatalog(), sprite.Texture.Id, AssetKind::Texture).GetTexture(), transform.GetMatrix(), tint };
			if (sprite.BlendMode == ECS::SpriteBlendMode::Opaque)
				m_OpaqueDraws.push_back(draw);
			else
				m_TransparentDraws.push_back({ draw, sprite.SortLayer, sprite.SortOrder });
		}
	}
}

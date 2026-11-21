#include "PerspectiveSample.hpp"
#include "Core/FrameTrace.hpp"
#include "SampleCamera.hpp"

#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/CameraView.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Raven
{
	namespace
	{
		std::optional<Vec3> IntersectGroundPlane(const Ray& ray)
		{
			if (ray.Direction.Z == 0.0f)
				return std::nullopt;

			const float distance = -ray.Origin.Z / ray.Direction.Z;
			if (!std::isfinite(distance) || distance < 0.0f)
				return std::nullopt;

			const Vec3 hit = ray.Origin + ray.Direction * distance;
			if (!std::isfinite(hit.X) || !std::isfinite(hit.Y))
				return std::nullopt;

			return Vec3{ hit.X, hit.Y, 0.0f };
		}

		void SetOrbitPose(ECS::TransformComponent& transform, Vec3 target,
			float yaw, float pitch, float distance)
		{
			const float horizontal = distance * std::cos(pitch);
			transform.Position = target + Vec3{
				horizontal * std::cos(yaw),
				horizontal * std::sin(yaw),
				distance * std::sin(pitch)
			};
			transform.Rotation = { -pitch, 0.0f, yaw + std::numbers::pi_v<float> / 2.0f };
		}

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

	PerspectiveSample::PerspectiveSample(VulkanContext& renderer, GameInstance& instance,
		bool freeFly, bool watchMesh, bool watchLevel, std::optional<AssetId> initialScene)
		: m_Renderer(renderer),
		  m_Game(instance),
		  m_FreeFly(freeFly),
		  m_Assets(renderer, watchLevel, watchMesh),
		  m_Audio(m_Assets.GetCatalog()),
		  m_Quad(renderer.CreateTexturedMesh(QuadVertices, QuadIndices)),
		  m_TexturedDraws{ VulkanTexturedDraw{ m_Quad, m_Assets.LoadTexture(SampleAssets::GetUVGrid()) },
			  VulkanTexturedDraw{ m_Quad, m_Assets.LoadTexture(SampleAssets::GetUVGridAlternate()) } },
		  m_TransparentDraws{ VulkanTransparentDraw{ m_TexturedDraws[0] },
			  VulkanTransparentDraw{ m_TexturedDraws[1] } }
	{
		m_TexturedDraws[0].Model = Translation({ -2.0f, 0.5f, 0.6f }) * Scale({ 1.6f, 1.0f, 1.6f });
		m_TexturedDraws[1].Model = Translation({ 2.0f, 0.5f, 0.6f }) * Scale({ 1.6f, 1.0f, 1.6f });
		m_TexturedDraws[1].Tint = { 0.8f, 1.0f, 0.8f, 1.0f };
		// The transparent quads overlap behind the imported meshes in the initial view.
		m_TransparentDraws[0].Draw.Model = Translation({ -0.45f, -1.55f, 0.65f }) * Scale({ 1.8f, 1.0f, 1.8f });
		m_TransparentDraws[0].Draw.Tint = { 1.0f, 0.65f, 0.65f, 0.45f };
		m_TransparentDraws[1].Draw.Model = Translation({ 0.45f, -1.15f, 0.85f }) * Scale({ 1.8f, 1.0f, 1.8f });
		m_TransparentDraws[1].Draw.Tint = { 0.65f, 0.8f, 1.0f, 0.65f };
		m_Session = std::make_unique<SceneSession>(m_Assets.GetProject(), SceneType::Perspective,
			[this](std::span<const std::byte> bytes, const std::filesystem::path& path, const AssetCatalog& catalog)
			{
				return std::make_unique<PerspectiveLevel>(m_Game.GetRegistry(), m_Assets.GetProject(),
					PerspectiveLevel::Decode(bytes, path), catalog);
			}, initialScene);
		m_BenchmarkRotor = GetLevel().FindEntity("benchmark-rotor-0");
		UpdateDraws();
	}

	PerspectiveSample::~PerspectiveSample() = default;

	GameBindings PerspectiveSample::GetGameBindings() const noexcept
	{
		if (m_FreeFly)
		{
			auto bindings = m_Session->GetActive().GetBindings();
			bindings.Interactions = m_Interactions;
			return bindings;
		}
		return {};
	}

	PerspectiveLevel& PerspectiveSample::GetLevel() const noexcept
	{
		return static_cast<PerspectiveLevel&>(m_Session->GetActive());
	}

	bool PerspectiveSample::PrepareFrame(const InputState& input, const FrameTime& frameTime)
	{
		const bool replaced = m_Session->Update(m_FreeFly && input.WasKeyPressed(Key::F3), input.WasKeyPressed(Key::F4));
		m_Interactions.clear();
		if (!replaced && frameTime.DeltaSeconds > 0) m_Interactions.swap(m_PendingInteractions);
		m_PendingInteractions.clear();
		if (replaced)
		{
			m_Audio.Stop(); m_CameraViewUnavailable = false; m_Angle = 0;
			m_BenchmarkRotor = GetLevel().FindEntity("benchmark-rotor-0");
			UpdateDraws();
		}
		return replaced;
	}

	void PerspectiveSample::OnFrame(VulkanContext& renderer,
		const InputState& input, const FrameTime& frameTime)
	{
		const auto frameStart = std::chrono::steady_clock::now();
		const auto reloaded = m_Assets.ApplyReloads();
		const bool replacedMesh = reloaded.Mesh;
		m_MeshReloadAwaitingPresent |= replacedMesh;
		Update(input, frameTime);
		if (auto* metrics = GetFrameMetrics(); metrics && !m_BenchmarkRotor.IsNull())
		{
			ECS::TransformComponent rotor;
			if (m_Game.GetRegistry().Read(m_BenchmarkRotor, rotor))
				metrics->BenchmarkPhase = rotor.Rotation.Y;
		}
		const auto renderedView = renderer.DrawFrame([this](std::uint32_t width, std::uint32_t height)
		{
			std::optional<CameraView> view;
			try
			{
				view = GetLevel().GetCameraView(GetLevel().GetSelectedCamera(), static_cast<float>(width) / height);
				m_CameraViewUnavailable = false;
			}
			catch (const std::invalid_argument& error)
			{
				ReportCameraUnavailable(m_CameraViewUnavailable, error.what());
			}
			return view;
		}, m_Draws, m_TexturedDraws, m_TransparentDraws, GetLevel().GetLighting());

		if (m_MeshReloadAwaitingPresent)
		{
			const auto present = std::chrono::steady_clock::now();
			const std::chrono::duration<double, std::milli> frame = present - frameStart;
			if (renderedView)
			{
				Log(LogLevel::Info, std::format("FBX reload presented: frame {:.3f} ms, clock {} ns",
					frame.count(), std::chrono::duration_cast<std::chrono::nanoseconds>(
						present.time_since_epoch()).count()));
				m_MeshReloadAwaitingPresent = false;
			}
			else if (replacedMesh)
				Log(LogLevel::Info, std::format("FBX reload frame skipped presentation: frame {:.3f} ms", frame.count()));
		}
		if (!renderedView)
			return;

		for (const MouseButtonPress& press : input.MouseButtonPresses)
		{
			if (press.Button != MouseButton::Left || press.X < 0 || press.Y < 0 ||
				static_cast<std::uint32_t>(press.X) >= press.FramebufferWidth ||
				static_cast<std::uint32_t>(press.Y) >= press.FramebufferHeight)
				continue;

			const double pixelX = (press.X + 0.5) * renderedView->Width / press.FramebufferWidth;
			const double pixelY = (press.Y + 0.5) * renderedView->Height / press.FramebufferHeight;
			const auto ray = ScreenPointToRay(renderedView->Camera,
				renderedView->Width, renderedView->Height, pixelX, pixelY);
			if (!ray)
				continue;

			if (m_FreeFly)
			{
				ECS::CameraComponent lens;
				if (m_Game.GetRegistry().Read(GetLevel().GetSelectedCamera(), lens))
					if (const auto distance = RayDistanceToFarPlane(renderedView->Camera, *ray, lens.FarPlane))
						if (const auto picked = GetLevel().Pick(renderer, *ray, *distance))
						{
							m_PendingInteractions.push_back(picked->Entity);
							Log(LogLevel::Info, "Picked " + GetLevel().GetAuthoredId(picked->Entity));
						}
			}
			if (const auto hit = IntersectGroundPlane(*ray))
				Log(LogLevel::Info, std::format("Ground hit ({:.3f}, {:.3f}, {:.3f})",
					hit->X, hit->Y, hit->Z));
		}
	}

	void PerspectiveSample::UpdateCamera(const InputState& input, const FrameTime& frameTime)
	{
		const float delta = static_cast<float>(std::min(frameTime.DeltaSeconds, 0.1));

		const bool reset = input.WasKeyPressed(Key::R) || input.WasKeyPressed(Key::Home);
		const float yawStep = CameraKeyStep(input, Key::Right, 0.1f, delta) -
			CameraKeyStep(input, Key::Left, 0.1f, delta);
		const float pitchStep = CameraKeyStep(input, Key::Up, 0.1f, delta) -
			CameraKeyStep(input, Key::Down, 0.1f, delta);
		if (!reset && yawStep == 0.0f && pitchStep == 0.0f && input.ScrollY == 0.0f)
			return;

		auto& registry = m_Game.GetRegistry();
		const auto& camera = GetLevel().GetCamera();
		ECS::TransformComponent transform;
		if (!registry.Read(camera.Entity, transform))
			return;

		if (reset)
			transform = camera.Spawn;

		if (yawStep != 0.0f || pitchStep != 0.0f || input.ScrollY != 0.0f)
		{
			const Vec3 offset = transform.Position - camera.OrbitTarget;
			const float horizontal = std::hypot(offset.X, offset.Y);
			const float radius = std::hypot(horizontal, offset.Z);
			if (!std::isfinite(radius) || radius == 0.0f)
				return;

			const float yaw = std::remainder(std::atan2(offset.Y, offset.X) + yawStep,
				2.0f * std::numbers::pi_v<float>);
			const float pitch = std::clamp(std::atan2(offset.Z, horizontal) + pitchStep, -1.3f, 1.3f);
			const float distance = std::clamp(radius - input.ScrollY * 0.5f, 2.5f, 25.0f);
			SetOrbitPose(transform, camera.OrbitTarget, yaw, pitch, distance);
		}

		if (!registry.Write(camera.Entity, transform))
			Log(LogLevel::Warning, "Cannot update perspective camera transform");
	}

	void PerspectiveSample::Update(const InputState& input, const FrameTime& frameTime)
	{
		if (input.WasKeyPressed(Key::Space))
			m_Animate = !m_Animate;
		if (input.WasKeyPressed(Key::O) || input.WasKeyPressed(Key::F1))
		{
			auto& order = m_TransparentDraws[1].SortOrder;
			order = order == 0 ? 1 : 0;
			Log(LogLevel::Info, order == 0 ? "Transparent order: automatic camera depth" :
				"Transparent order: blue-tinted quad composited last");
		}

		if (input.WasKeyPressed(Key::F6) && GetLevel().SwapMeshSources())
			Log(LogLevel::Info, "Swapped Perspective mesh sources");
		if (!m_FreeFly)
			UpdateCamera(input, frameTime);
		else if (GetLevel().ConsumeCompletion())
		{
			Log(LogLevel::Info, "Goal completed. F3 restarts.");
			if (const auto* sound = GetLevel().GetWinSound())
				m_Audio.Play(*sound);
		}

		if (m_Animate && !m_FreeFly)
		{
			m_Angle = std::fmod(m_Angle + frameTime.DeltaSeconds * 0.7,
				2.0 * std::numbers::pi_v<double>);
			GetLevel().AnimateMeshes(static_cast<float>(m_Angle), !m_FreeFly);
		}
		UpdateDraws();
	}

	void PerspectiveSample::UpdateDraws()
	{
		auto* metrics = GetFrameMetrics();
		FrameSection gather(metrics ? &metrics->SceneGather : nullptr);
		auto& level = GetLevel();
		{
			// Host PostUpdate: refresh cached world transforms changed since the last frame.
			FrameSection transformTiming(metrics ? &metrics->WorldTransforms : nullptr);
			level.UpdateWorldTransforms();
		}
		const auto isFinite = [](const Mat4& matrix)
		{
			for (const auto& column : matrix.Columns)
				for (const float value : column)
					if (!std::isfinite(value))
						return false;
			return true;
		};
		m_Draws.clear();
		const auto scene = level.GetAccess();
		scene.View<const ECS::WorldTransformComponent, const ECS::StaticMeshComponent>().Each(
			[&](ECS::Entity entity, const ECS::WorldTransformComponent& world, const ECS::StaticMeshComponent& mesh)
			{
				const auto* resource = m_Renderer.ResolveMesh(mesh.Mesh);
				if (!resource || !isFinite(world.World))
					return;
				const auto* found = scene.TryGet<const ECS::MaterialComponent>(entity);
				const ECS::MaterialComponent material = found ? *found : ECS::MaterialComponent{};
				const VulkanTexture* texture = nullptr;
				if (material.BaseColorTexture.IsValid())
					texture = &level.UseAsset(m_Assets.GetCatalog(), material.BaseColorTexture.Id, AssetKind::Texture).GetTexture();
				const Vec4 color{ mesh.Tint.X * material.BaseColor.X, mesh.Tint.Y * material.BaseColor.Y,
					mesh.Tint.Z * material.BaseColor.Z, mesh.Tint.W * material.BaseColor.W };
				m_Draws.push_back({ *resource, world.World, color, texture, material.Lit });
			});
	}
}

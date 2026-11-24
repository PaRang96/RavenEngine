#include "Core/GameSession.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/LightingComponent.hpp"
#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/RenderWorld.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneFile.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <stdexcept>
#include <utility>

namespace Raven
{
	namespace
	{
		// An XZ quad facing -Y; sprites scale it. Images are top-row first, so V=0 is the top edge.
		constexpr std::array<TexturedVertex, 4> QuadVertices
		{
			TexturedVertex{ { -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f } },
			TexturedVertex{ { 0.5f, 0.0f, 0.5f }, { 1.0f, 0.0f } },
			TexturedVertex{ { -0.5f, 0.0f, 0.5f }, { 0.0f, 0.0f } }
		};
		constexpr std::array<std::uint16_t, 6> QuadIndices{ 0, 1, 2, 2, 3, 0 };

		std::filesystem::path ResolveContentRoot(const std::filesystem::path& root)
		{
			if (root.empty())
				return Paths::GetContentPath();
			return root.is_absolute() ? root : Paths::GetExecutableDirectory() / root;
		}

		// Leases every asset the file references, so a scene that names a missing file is rejected
		// before it replaces the active one, and drawing never loads synchronously.
		void LeaseAssets(const SceneFile& file, Scene& scene, const AssetCatalog& catalog)
		{
			static const std::array<std::pair<std::uint64_t, AssetKind>, 3> kinds{ {
				{ ECS::Detail::HashText(AssetTypeTraits<TextureAsset>::Name), AssetKind::Texture },
				{ ECS::Detail::HashText(AssetTypeTraits<MeshAsset>::Name), AssetKind::Mesh },
				{ ECS::Detail::HashText(AssetTypeTraits<AudioAsset>::Name), AssetKind::Audio } } };
			std::map<AssetId, AssetKind> used;
			for (const auto& component : file.Components)
				for (std::size_t field = 0; field < component.Fields.size(); ++field)
				{
					const auto& description = component.Fields[field];
					const auto kind = std::find_if(kinds.begin(), kinds.end(),
						[&](const auto& entry) { return entry.first == description.Tag; });
					if (description.Kind != ECS::FieldKind::AssetRef || kind == kinds.end())
						continue;
					const auto count = description.Extent[0] * description.Extent[1];
					const auto* values = component.Values.data() + component.FieldOffset(field);
					for (std::size_t item = 0; item < component.Entities.size() * count; ++item)
					{
						AssetId id;
						std::memcpy(&id, values + item * sizeof(AssetId), sizeof(AssetId));
						if (id.IsValid())
							used.emplace(id, kind->second);
					}
				}
			for (const auto& [id, kind] : used)
				scene.UseAsset(catalog, id, kind);
		}
	}

	GameSession::GameSession(VulkanContext& renderer, GameInstance& game, const ApplicationConfig& config)
		: m_Renderer(renderer),
		  m_Game(game),
		  m_Options(config.GameOptions),
		  m_Project(renderer, ResolveContentRoot(config.ContentRoot), config.WatchContent, config.WatchContent,
			  config.WatchContent),
		  m_Audio(m_Project.GetCatalog()),
		  m_Session(std::make_unique<SceneSession>(m_Project,
			  [this](std::span<const std::byte> bytes, const std::filesystem::path&, const AssetCatalog& catalog)
			  {
				  if (!IsSceneFile(bytes))
					  throw std::runtime_error("not a scene file; convert it with RavenSceneTool");
				  const auto file = SceneFile::Decode(bytes);
				  auto scene = std::make_unique<Scene>(m_Game.GetRegistry(), m_Project.GetAssets());
				  InstantiateScene(file, *scene);
				  LeaseAssets(file, *scene, catalog);
				  return scene;
			  }, config.StartScene)),
		  m_Commands(m_Session->GetActive()),
		  m_Quad(renderer.CreateTexturedMesh(QuadVertices, QuadIndices))
	{
		if (config.WatchShaders)
			renderer.WatchShaders();
	}

	GameSession::~GameSession() = default;

	Scene& GameSession::GetScene() const noexcept
	{
		return m_Session->GetActive();
	}

	const AssetCatalog& GameSession::GetCatalog() const noexcept
	{
		return m_Project.GetCatalog();
	}

	bool GameSession::Prepare(const InputState& input, const FrameTime& time)
	{
		if (input.WasKeyPressed(Key::F8))
		{
			m_Playing = !m_Playing;
			Log(LogLevel::Info, m_Playing ? "Play resumed (F8)" : "Play paused (F8): game systems do not run");
		}
		// F7 cycles the lit-mesh debug views (docs/HotReloadDesign.md, H3).
		if (input.WasKeyPressed(Key::F7))
		{
			constexpr std::array<std::pair<RenderView, const char*>, 4> views{ {
				{ RenderView::Shaded, "shaded" }, { RenderView::Unlit, "unlit" },
				{ RenderView::Normals, "normals" }, { RenderView::Wireframe, "wireframe" } } };
			auto index = static_cast<std::size_t>(m_Renderer.GetRenderView());
			do
				index = (index + 1) % views.size();
			while (!m_Renderer.SetRenderView(views[index].first));
			Log(LogLevel::Info, std::string("Render view: ") + views[index].second);
		}
		const bool replaced = Advance(input.WasKeyPressed(Key::F3), input.WasKeyPressed(Key::F4));
		// Picks of the presented frame reach the next update once; a replaced scene or the first
		// frame after a pause drops them.
		m_Picks.clear();
		if (!replaced && time.DeltaSeconds > 0)
			m_Picks.swap(m_PendingPicks);
		m_PendingPicks.clear();
		const auto reloaded = m_Project.GetAssets().ApplyReloads();
		if (reloaded.Mesh || reloaded.Texture)
			m_ReloadApplied = std::chrono::steady_clock::now();
		m_MeshReloadAwaitingPresent |= reloaded.Mesh;
		m_TextureReloadAwaitingPresent |= reloaded.Texture;
		return replaced;
	}

	bool GameSession::Advance(bool restart, bool next)
	{
		if (!m_Session->Update(restart, next))
			return false;
		m_Commands.Reset(m_Session->GetActive());
		m_Picks.clear();
		m_PendingPicks.clear();
		m_Audio.Stop();
		m_CameraProblem.clear();
		return true;
	}

	SystemContext GameSession::MakeContext(const InputState& input, const FrameTime& time)
	{
		return SystemContext{ m_Session->GetActive().GetAccess(), input, time, m_Picks, m_Commands, *this };
	}

	void GameSession::ApplyCommands()
	{
		m_Commands.Apply();
	}

	void GameSession::PlaySound(AssetRef<AudioAsset> sound)
	{
		// The scene leased its sounds at load, through the store that resolves paths against the
		// game's content root; a reference it does not hold is a data error, logged once (FD-06).
		if (!sound.IsValid())
			return;
		auto& scene = m_Session->GetActive();
		const auto* lease = scene.FindAsset(sound.Id);
		if (!lease)
			lease = scene.TryUseAsset(m_Project.GetCatalog(), sound.Id, AssetKind::Audio);
		if (lease)
			m_Audio.Play(*lease);
	}

	void GameSession::StopSounds() noexcept
	{
		m_Audio.Stop();
	}

	bool GameSession::HasOption(std::string_view name) const noexcept
	{
		return std::find(m_Options.begin(), m_Options.end(), name) != m_Options.end();
	}

	void GameSession::ReportCamera(const std::string& message)
	{
		if (m_CameraProblem != message)
			Log(LogLevel::Warning, "No camera view: " + message);
		m_CameraProblem = message;
	}

	void GameSession::GatherSprites()
	{
		m_OpaqueSprites.clear();
		m_TransparentSprites.clear();
		auto& scene = m_Session->GetActive();
		const auto& catalog = m_Project.GetCatalog();
		scene.GetAccess().View<const ECS::SpriteComponent, const ECS::WorldTransformComponent>().Each(
			[&](ECS::Entity, const ECS::SpriteComponent& sprite, const ECS::WorldTransformComponent& world)
			{
				if (!sprite.Texture.IsValid() ||
					(sprite.BlendMode != ECS::SpriteBlendMode::Opaque && sprite.BlendMode != ECS::SpriteBlendMode::Alpha))
					return;
				// A bad game-written texture reference skips the sprite (FD-06).
				const auto* lease = scene.FindAsset(sprite.Texture.Id);
				if (!lease || lease->GetKind() != AssetKind::Texture)
					lease = scene.TryUseAsset(catalog, sprite.Texture.Id, AssetKind::Texture);
				if (!lease)
					return;
				const VulkanTexturedDraw draw{ m_Quad, lease->GetTexture(), world.World, sprite.Tint };
				if (sprite.BlendMode == ECS::SpriteBlendMode::Opaque)
					m_OpaqueSprites.push_back(draw);
				else
					m_TransparentSprites.push_back({ draw, sprite.SortLayer, sprite.SortOrder });
			});
	}

	bool GameSession::Render(VulkanContext& renderer, RenderWorld& world, const InputState& input)
	{
		auto& scene = m_Session->GetActive();
		const auto access = scene.GetAccess();
		// The engine tag names the camera; without one, the first camera of the scene is used.
		ECS::Entity camera;
		access.View<const ECS::ActiveCameraComponent, const ECS::CameraComponent>().Each(
			[&camera](ECS::Entity entity, const ECS::ActiveCameraComponent&, const ECS::CameraComponent&)
			{
				camera = entity;
				return false;
			});
		if (camera.IsNull())
			access.View<const ECS::CameraComponent>().Each([&camera](ECS::Entity entity, const ECS::CameraComponent&)
			{
				camera = entity;
				return false;
			});
		SceneLighting lighting;
		access.View<const ECS::LightingComponent>().Each([&lighting](ECS::Entity, const ECS::LightingComponent& value)
		{
			lighting = value.GetLighting();
			return false;
		});
		GatherSprites();
		const auto rendered = renderer.DrawFrame([&](std::uint32_t width, std::uint32_t height) -> std::optional<CameraView>
		{
			if (camera.IsNull())
			{
				ReportCamera("the scene has no camera");
				return std::nullopt;
			}
			try
			{
				const auto view = scene.GetCameraView(camera, static_cast<float>(width) / static_cast<float>(height));
				m_CameraProblem.clear();
				return view;
			}
			catch (const std::invalid_argument& error)
			{
				ReportCamera(error.what());
				return std::nullopt;
			}
		}, &world, m_OpaqueSprites, m_TransparentSprites, lighting);

		if (m_MeshReloadAwaitingPresent || m_TextureReloadAwaitingPresent)
		{
			const auto present = std::chrono::steady_clock::now();
			const std::chrono::duration<double, std::milli> frame = present - m_ReloadApplied;
			const auto clock = std::chrono::duration_cast<std::chrono::nanoseconds>(present.time_since_epoch()).count();
			if (rendered)
			{
				if (m_MeshReloadAwaitingPresent)
					Log(LogLevel::Info, std::format("FBX reload presented: frame {:.3f} ms, clock {} ns", frame.count(), clock));
				if (m_TextureReloadAwaitingPresent)
					Log(LogLevel::Info, std::format("Texture reload presented: frame {:.3f} ms, clock {} ns", frame.count(), clock));
				m_MeshReloadAwaitingPresent = m_TextureReloadAwaitingPresent = false;
			}
		}
		if (!rendered)
			return false;

		const auto* lens = scene.FindComponent<ECS::CameraComponent>(camera);
		for (const MouseButtonPress& press : input.MouseButtonPresses)
		{
			if (!lens || press.Button != MouseButton::Left || press.X < 0 || press.Y < 0 ||
				static_cast<std::uint32_t>(press.X) >= press.FramebufferWidth ||
				static_cast<std::uint32_t>(press.Y) >= press.FramebufferHeight)
				continue;
			const double pixelX = (press.X + 0.5) * rendered->Width / press.FramebufferWidth;
			const double pixelY = (press.Y + 0.5) * rendered->Height / press.FramebufferHeight;
			const auto ray = ScreenPointToRay(rendered->Camera, rendered->Width, rendered->Height, pixelX, pixelY);
			if (!ray)
				continue;
			const auto distance = RayDistanceToFarPlane(rendered->Camera, *ray, lens->FarPlane);
			if (!distance)
				continue;
			if (const auto picked = Pick(scene, renderer, *ray, *distance))
			{
				m_PendingPicks.push_back(*picked);
				Log(LogLevel::Info, "Picked " + scene.GetAuthoredId(picked->Entity));
			}
		}
		return true;
	}
}

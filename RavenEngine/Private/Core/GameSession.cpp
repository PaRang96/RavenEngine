#include "Core/GameSession.hpp"
#include "Renderer/Vulkan/VulkanTexture.hpp"
#include "Core/FrameTrace.hpp"
#include "Audio/SoundEvent.hpp"

#include "Physics/PickWorld.hpp"
#include "Renderer/DrawChecks.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"

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
#include <random>
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
		// Scene restarts, quick-loads and StopSounds fade every sound out over 50 ms.
		constexpr float StopFadeSeconds = 0.05f;

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
			static const std::array<std::pair<std::uint64_t, AssetKind>, 4> kinds{ {
				{ ECS::Detail::HashText(AssetTypeTraits<TextureAsset>::Name), AssetKind::Texture },
				{ ECS::Detail::HashText(AssetTypeTraits<MeshAsset>::Name), AssetKind::Mesh },
				{ ECS::Detail::HashText(AssetTypeTraits<AudioAsset>::Name), AssetKind::Audio },
				{ ECS::Detail::HashText(AssetTypeTraits<SoundAsset>::Name), AssetKind::Sound } } };
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
			{
				scene.UseAsset(catalog, id, kind);
				// A sound event's clips are leased with it, so playing it never loads a file.
				if (kind == AssetKind::Sound)
					if (const auto* lease = scene.FindAsset(id); lease && lease->GetSoundEvent())
						for (const auto& clip : lease->GetSoundEvent()->Clips)
							scene.UseAsset(catalog, clip, AssetKind::Audio);
			}
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
				  // Leased first, so a missing or broken asset fails before the costly instantiation.
				  LeaseAssets(file, *scene, catalog);
				  InstantiateScene(file, *scene);
				  return scene;
			  }, config.StartScene)),
		  m_Commands(m_Session->GetActive()),
		  m_Quad(renderer.CreateTexturedMesh(QuadVertices, QuadIndices))
	{
		if (config.WatchShaders)
			renderer.WatchShaders();
		if (config.SimulationSeed)
			m_SimulationSeed = *config.SimulationSeed;
		else
		{
			std::random_device device;
			m_SimulationSeed = (std::uint64_t{ device() } << 32) ^ device();
		}
		Log(LogLevel::Info, std::format("Simulation seed {}", m_SimulationSeed));
		StartSimulation();
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

	bool GameSession::Prepare(const DeveloperRequests& requests, const FrameTime& time)
	{
		if (requests.RestartPlay && !m_Playing)
		{
			m_Playing = true;
			Log(LogLevel::Info, "Play restarted");
		}
		else if (requests.TogglePlay)
		{
			m_Playing = !m_Playing;
			Log(LogLevel::Info, m_Playing ? "Play resumed" : "Play paused: game systems do not run");
		}
		// Cycles the lit-mesh debug views (docs/HotReloadDesign.md, H3).
		if (requests.CycleRenderView)
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
		const bool replaced = Advance(requests.RestartScene || requests.RestartPlay, requests.NextScene);
		if (replaced)
			StartSimulation();
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
		bool replaced = false;
		// A developer key wins this frame; a game request then waits for the next one.
		if (restart || next || !m_SceneRequest)
			replaced = m_Session->Update(restart, next);
		else
		{
			const auto request = *std::exchange(m_SceneRequest, std::nullopt);
			using Kind = SceneRequest::Kind;
			replaced = request.Type == Kind::Scene ? m_Session->Load(request.Scene) :
				m_Session->Update(request.Type == Kind::Restart, request.Type == Kind::Next);
			m_Transition = replaced ? SceneTransition::Idle : SceneTransition::Failed;
		}
		if (!replaced)
			return false;
		m_Commands.Reset(m_Session->GetActive());
		m_Picks.clear();
		m_PendingPicks.clear();
		m_Audio.StopAll(StopFadeSeconds);
		m_CameraProblem.clear();
		return true;
	}

	void GameSession::QuickSave(std::uint64_t tick)
	{
		const auto& scene = m_Session->GetActive();
		const auto file = CaptureScene(scene);
		Snapshot snapshot;
		snapshot.Scene = m_Session->GetActiveDescription().Id;
		snapshot.Bytes = file.Encode();
		snapshot.Random = m_Random.GetState();
		snapshot.Ids = scene.GetIdState();
		snapshot.Tick = tick;
		snapshot.Entities = file.Entities.size();
		Log(LogLevel::Info, std::format("Quick-saved tick {} ({} entities, {:.1f} KB)", tick, snapshot.Entities,
			static_cast<double>(snapshot.Bytes.size()) / 1024.0));
		m_QuickSave = std::move(snapshot);
	}

	std::optional<std::uint64_t> GameSession::QuickLoad()
	{
		if (!m_QuickSave)
		{
			Log(LogLevel::Info, "No quick-save to load; Ctrl+F5 saves one");
			return std::nullopt;
		}
		if (m_QuickSave->Scene != m_Session->GetActiveDescription().Id)
		{
			Log(LogLevel::Warning, "The quick-save belongs to another scene; open it first");
			return std::nullopt;
		}
		try
		{
			m_Session->RestoreSnapshot(m_QuickSave->Bytes);
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Error, std::string("Quick-load failed; the world stays as it was: ") + error.what());
			return std::nullopt;
		}
		m_Commands.Reset(m_Session->GetActive());
		m_Picks.clear();
		m_PendingPicks.clear();
		m_Audio.StopAll(StopFadeSeconds);
		m_CameraProblem.clear();
		m_Random.SetState(m_QuickSave->Random);
		m_Session->GetActive().SeedIds(m_QuickSave->Ids);
		Log(LogLevel::Info, std::format("Quick-loaded tick {} ({} entities)", m_QuickSave->Tick, m_QuickSave->Entities));
		return m_QuickSave->Tick;
	}

	void GameSession::SetSimulationPaused(bool paused) noexcept
	{
		if (paused != m_SimulationPaused)
			Log(LogLevel::Info, paused ? "Simulation paused by the game" : "Simulation resumed by the game");
		m_SimulationPaused = paused;
	}

	void GameSession::RequestScene(AssetRef<SceneAsset> scene) noexcept
	{
		m_SceneRequest = SceneRequest{ SceneRequest::Kind::Scene, scene.Id };
		m_Transition = SceneTransition::Loading;
	}

	void GameSession::RequestNextScene() noexcept
	{
		m_SceneRequest = SceneRequest{ SceneRequest::Kind::Next, {} };
		m_Transition = SceneTransition::Loading;
	}

	void GameSession::RequestRestart() noexcept
	{
		m_SceneRequest = SceneRequest{ SceneRequest::Kind::Restart, {} };
		m_Transition = SceneTransition::Loading;
	}

	SceneTransition GameSession::GetSceneTransition() const noexcept
	{
		return m_Transition;
	}

	UserDataStore& GameSession::GetUserData()
	{
		if (!m_UserData)
			throw std::runtime_error("This host has no user data folder");
		return *m_UserData;
	}

	SystemContext GameSession::MakeContext(const InputState& input, InputActions& actions, InputStream& stream,
		const FrameTime& time)
	{
		// Without a physics world (tools and smoke tests) every query misses.
		static const struct NoPhysics final : PhysicsQueries
		{
			std::optional<PhysicsHit> CastRay(Vec3, Vec3, float, std::uint32_t) const override { return std::nullopt; }
			std::optional<PhysicsHit> CastShape(const PhysicsShape&, Vec3, float, std::uint32_t) const override
			{
				return std::nullopt;
			}
			std::span<const ECS::Entity> Overlap(const PhysicsShape&, std::uint32_t) const override { return {}; }
			std::span<const ContactEvent> GetContacts() const noexcept override { return {}; }
		} none;
		// Tools and smoke tests run without a UI; declarations then go nowhere.
		static struct NoUi final : UI::Ui
		{
			void Begin(std::string_view, const UI::Style&) override {}
			void BeginKey(std::string_view, std::uint64_t, const UI::Style&) override {}
			void End() override {}
			void Text(std::string_view, std::string_view, const UI::Style&) override {}
			UI::Interaction Button(std::string_view, std::string_view, const UI::Style&) override { return {}; }
			UI::Interaction Toggle(std::string_view, std::string_view, bool&, const UI::Style&) override { return {}; }
			void Image(std::string_view, AssetRef<TextureAsset>, const UI::Style&) override {}
			void Focus(std::string_view) override {}
			bool IsCapturingInput() const noexcept override { return false; }
			bool IsRightToLeft() const noexcept override { return false; }
		} noUi;
		UI::Ui& ui = m_Ui ? static_cast<UI::Ui&>(*m_Ui) : static_cast<UI::Ui&>(noUi);
		return SystemContext{ m_Session->GetActive().GetAccess(), input, actions, stream, time, m_Picks,
			m_Physics ? *m_Physics : none, m_Commands, *this, m_Random, ui };
	}

	void GameSession::SetUi(Text::TextSystem* text, UI::UiContext* game, UI::UiContext* developer)
	{
		m_Text = text;
		m_Ui = game;
		m_DeveloperUi = developer;
		if (!m_Ui)
			return;
		// UI images use the active scene's texture leases, like sprites; a bad reference draws nothing.
		m_Ui->SetTextureResolver([this](const AssetId& id) -> std::optional<std::uint32_t>
		{
			auto& scene = m_Session->GetActive();
			const auto* lease = scene.FindAsset(id);
			if (!lease || lease->GetKind() != AssetKind::Texture)
				lease = scene.TryUseAsset(m_Project.GetCatalog(), id, AssetKind::Texture);
			if (!lease)
				return std::nullopt;
			return lease->GetTexture().GetBindlessIndex();
		});
	}

	double GameSession::GetFixedStepRate() const noexcept
	{
		return m_Project.GetFixedStepRate();
	}

	const PhysicsLimits& GameSession::GetPhysicsLimits() const noexcept
	{
		return m_Project.GetPhysicsLimits();
	}

	void GameSession::StartSimulation() noexcept
	{
		m_Random.SetState(m_SimulationSeed);
		// A different stream for IDs, so drawing random numbers never shifts which IDs spawns get.
		m_Session->GetActive().SeedIds(m_SimulationSeed ^ 0xA5A5A5A5DEADBEEFull);
	}

	void GameSession::ApplyCommands()
	{
		m_Commands.Apply();
	}

	void GameSession::DropCommands() noexcept
	{
		const auto pending = m_Commands.GetPendingCount();
		m_Commands.Reset(m_Session->GetActive());
		if (pending != 0)
			try
			{
				Log(LogLevel::Warning, std::format("Dropped {} scene command(s) recorded before the gameplay stage failed",
					pending));
			}
			catch (...)
			{
			}
	}

	SoundHandle GameSession::PlaySound(AssetRef<AudioAsset> sound, const SoundParams& params)
	{
		// The scene leased its sounds at load, through the store that resolves paths against the
		// game's content root; a reference it does not hold is a data error, logged once (FD-06).
		if (!sound.IsValid())
			return {};
		auto& scene = m_Session->GetActive();
		const auto* lease = scene.FindAsset(sound.Id);
		if (!lease)
			lease = scene.TryUseAsset(m_Project.GetCatalog(), sound.Id, AssetKind::Audio);
		return lease ? m_Audio.Play(*lease, params) : SoundHandle{};
	}

	SoundHandle GameSession::PlaySound(AssetRef<SoundAsset> sound, const SoundOverrides& overrides)
	{
		if (!sound.IsValid())
			return {};
		auto& scene = m_Session->GetActive();
		const auto& catalog = m_Project.GetCatalog();
		const auto* lease = scene.FindAsset(sound.Id);
		if (!lease)
			lease = scene.TryUseAsset(catalog, sound.Id, AssetKind::Sound);
		const auto* event = lease ? lease->GetSoundEvent() : nullptr;
		if (!event || event->Clips.empty())
			return {};
		std::uniform_int_distribution<std::size_t> pick(0, event->Clips.size() - 1);
		const auto clipId = event->Clips[pick(m_SoundRandom)];
		const auto* clip = scene.FindAsset(clipId);
		if (!clip)
			clip = scene.TryUseAsset(catalog, clipId, AssetKind::Audio);
		if (!clip)
			return {};
		const auto range = [this](float low, float high)
		{
			return low >= high ? low : std::uniform_real_distribution<float>(low, high)(m_SoundRandom);
		};
		SoundParams params = event->Defaults;
		params.Volume = range(event->VolumeMin, event->VolumeMax) * overrides.Volume;
		params.Pitch = range(event->PitchMin, event->PitchMax) * overrides.Pitch;
		params.Position = overrides.Position;
		return m_Audio.Play(*clip, params);
	}

	void GameSession::StopSounds() noexcept
	{
		m_Audio.StopAll(StopFadeSeconds);
	}

	bool GameSession::HasOption(std::string_view name) const noexcept
	{
		return std::find(m_Options.begin(), m_Options.end(), name) != m_Options.end();
	}

	std::optional<PickHit> GameSession::Pick(const Ray& ray, float maxDistance)
	{
		if (!m_Picking)
			return std::nullopt;
		auto& scene = m_Session->GetActive();
		// Normally a no-op: the frame's world pass already ran. Tools and tests may pick between frames.
		scene.UpdateWorldTransforms();
		// Only meshes the scene already leases can be hit; picking never loads an asset.
		const PickMeshResolver meshes = [&](AssetId id) -> PickMesh
		{
			const auto* lease = scene.FindAsset(id);
			if (!lease || lease->GetKind() != AssetKind::Mesh)
				return {};
			const auto* mesh = m_Renderer.ResolveMesh(lease->GetMesh());
			if (!mesh || !mesh->GetData())
				return {};
			const auto& data = mesh->GetData();
			return { data, data->PickVertices, data->PickIndices };
		};
		return m_Picking->Pick(scene, m_Game.GetRegistry(), meshes, ray, maxDistance);
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
		std::size_t skipped = 0;
		scene.GetAccess().View<const ECS::SpriteComponent, const ECS::WorldTransformComponent>().Each(
			[&](ECS::Entity, const ECS::SpriteComponent& sprite, const ECS::WorldTransformComponent& world)
			{
				if (!sprite.Texture.IsValid() ||
					(sprite.BlendMode != ECS::SpriteBlendMode::Opaque && sprite.BlendMode != ECS::SpriteBlendMode::Alpha))
					return;
				// A non-finite world or tint is skipped, not thrown in the draw queue (FD-06).
				if (!IsDrawable(world.World) || !IsFinite(sprite.Tint))
				{
					++skipped;
					return;
				}
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
		if (skipped != m_SkippedSprites && skipped != 0)
			Log(LogLevel::Warning, std::format("{} sprite(s) not drawn: a non-finite world transform or tint", skipped));
		m_SkippedSprites = skipped;
	}

	OverlayFrame GameSession::BuildOverlay()
	{
		// The game's UI, then the developer overlay over it (docs/UIDesign.md, section 4).
		auto* metrics = GetFrameMetrics();
		FrameSection timing(metrics ? &metrics->Ui : nullptr);
		m_Overlay.clear();
		if (m_Ui)
			m_Ui->EndFrame(m_Overlay);
		if (m_DeveloperUi)
			m_DeveloperUi->EndFrame(m_Overlay);
		if (metrics)
			metrics->UiQuads = static_cast<std::uint32_t>(m_Overlay.size());
		if (!m_Text)
			return { m_Overlay, {} };
		return { m_Overlay, m_Text->TakeUploads() };
	}

	bool GameSession::Render(VulkanContext& renderer, RenderWorld& world, const InputState& input,
		std::span<const DebugLineVertex> debugLines)
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
		// Spatial sounds are heard from the camera that draws the frame.
		if (const auto* view = camera.IsNull() ? nullptr : access.TryGet<const ECS::WorldTransformComponent>(camera))
			m_Audio.SetListener({ view->World(0, 3), view->World(1, 3), view->World(2, 3) },
				{ view->World(0, 0), view->World(1, 0), view->World(2, 0) });
		else
			m_Audio.ClearListener();
		m_Audio.Update();
		SceneLighting lighting;
		access.View<const ECS::LightingComponent>().Each([&lighting](ECS::Entity, const ECS::LightingComponent& value)
		{
			lighting = value.GetLighting();
			return false;
		});
		// Authored lighting can hold any finite value (dump/apply); invalid lighting draws with the
		// default and is reported once until it changes (FD-06).
		try
		{
			lighting.Validate();
			m_LightingProblem.clear();
		}
		catch (const std::invalid_argument& error)
		{
			if (m_LightingProblem != error.what())
				Log(LogLevel::Warning, std::string("Scene lighting ignored, the default is used: ") + error.what());
			m_LightingProblem = error.what();
			lighting = SceneLighting{};
		}
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
		}, &world, m_OpaqueSprites, m_TransparentSprites, lighting, debugLines, BuildOverlay());
		if (m_Text)
			m_Text->EndFrame(rendered.has_value());

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
		for (const InputEvent& press : input.Events)
		{
			if (!lens || press.Code != InputCode::FromMouse(MouseButton::Left) ||
				press.Transition != InputTransition::Pressed || press.X < 0 || press.Y < 0 ||
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
			if (const auto picked = Pick(*ray, *distance))
			{
				m_PendingPicks.push_back(*picked);
				const auto stats = m_Picking->GetStats();
				Log(LogLevel::Info, std::format("Picked {} (pick sync {:.3f} ms, query {:.3f} ms, {} bodies)",
					scene.GetAuthoredId(picked->Entity), static_cast<double>(stats.SyncNanoseconds) / 1e6,
					static_cast<double>(stats.QueryNanoseconds) / 1e6, stats.Bodies));
			}
		}
		return true;
	}
}

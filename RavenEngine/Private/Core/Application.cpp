#include "Raven/Core/Application.hpp"
#include "Core/DataError.hpp"
#include "Core/ReloadService.hpp"
#include "Core/FrameClock.hpp"
#include "Core/FrameTrace.hpp"
#include "Core/FixedGameplayStep.hpp"
#include "Core/GameSession.hpp"
#include "Core/Paths.hpp"
#include "Core/UserData.hpp"
#include "Input/InputBindingsFile.hpp"
#include "Input/InputRouter.hpp"
#include "Core/JobSystem.hpp"
#include "Core/LogHistory.hpp"
#include "Text/TextSystem.hpp"
#include "UI/UiContext.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/PickWorld.hpp"
#include "Scene/TransformInterpolator.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/GameModule.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Renderer/RenderWorld.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneFile.hpp"

#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iomanip>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Raven/Platform/Window.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

namespace Raven
{
	namespace
	{
		thread_local FrameMetrics* CurrentMetrics = nullptr;

		class ActiveFrameMetrics
		{
		public:
			ActiveFrameMetrics(bool capture, TraceClock::time_point start)
			{
				if (capture)
				{
					Metrics.emplace().Start = TraceNanoseconds(start.time_since_epoch());
					CurrentMetrics = &*Metrics;
				}
			}
			~ActiveFrameMetrics() { CurrentMetrics = nullptr; }
			std::optional<FrameMetrics> Metrics;
		};

		struct FrameSample
		{
			FrameMetrics Metrics;
			std::int64_t Work;
			bool Drawable;
		};

		// The recoverable boundary around each host update stage (docs/DataModelDesign.md, FD-06).
		// A data or I/O failure is logged and the frame continues with the stage's last good
		// state. Exhaustion, a broken invariant (std::logic_error) and non-standard exceptions
		// still end the process, because continuing would hide corruption. A stage that keeps
		// failing with the same message logs once, until it succeeds again.
		//
		// Render recovers only DataError, which is raised while the frame is gathered, before the
		// swapchain image is acquired. Any other failure there (device loss, a failure after
		// acquisition) would leave the image and its semaphore unconsumed, so it stays fatal.
		class StageBoundary
		{
		public:
			enum Stage : std::size_t { Prepare, Gameplay, PostUpdate, Extract, Render, Count };

			template<typename Function>
			bool Run(Stage stage, Function&& function)
			{
				try
				{
					function();
				}
				catch (const std::bad_alloc&)
				{
					throw;
				}
				catch (const std::logic_error&)
				{
					throw;
				}
				catch (const DataError& error)
				{
					return Fail(stage, error);
				}
				catch (const std::exception& error)
				{
					if (stage == Render)
						throw;
					return Fail(stage, error);
				}
				auto& state = m_States[stage];
				if (!state.Message.empty())
				{
					Log(LogLevel::Info, std::string(Names[stage]) + " recovered after " +
						std::to_string(state.Repeats + 1) + " failed frame(s)");
					state = {};
				}
				return true;
			}

		private:
			bool Fail(Stage stage, const std::exception& error)
			{
				auto& state = m_States[stage];
				if (state.Message != error.what())
				{
					state.Message = error.what();
					Log(LogLevel::Error, std::string(Names[stage]) + " failed; the frame continues with its last good state: " +
						state.Message);
				}
				else
					++state.Repeats;
				return false;
			}

			static constexpr const char* Names[Count]{ "Scene preparation", "Gameplay",
				"World transform update", "Render world sync", "Render" };
			struct State
			{
				std::string Message;
				std::uint64_t Repeats = 0;
			};
			State m_States[Count];
		};
	}

	FrameMetrics* GetFrameMetrics() noexcept { return CurrentMetrics; }

	namespace
	{
		// The UI preview mode (docs/UIDesign.md, layer 1): one registered entry alone, without playing,
		// with the switches that expose layout problems early.
		struct UiPreview
		{
			static constexpr float Scales[] = { 1.0f, 1.5f, 2.0f, 0.75f };
			// -1 while the game plays.
			int Entry = -1;
			std::size_t Scale = 0;
			bool RightToLeft = false;
			bool Pseudo = false;
		};

		// On-screen diagnostics: warnings and errors of the last ten seconds, such as failed reloads,
		// and the preview's state, drawn by the developer UI context over the game's.
		// The determinism check (docs/GameServicesDesign.md, FD-48): a scripted key lane drives the real
		// frame loop one fixed tick per frame; the scene is hashed after every tick, restarted with the
		// same seed, played again and compared.
		class DeterminismCheck
		{
		public:
			explicit DeterminismCheck(std::uint32_t ticks) : m_Ticks(ticks) {}

			bool IsActive() const noexcept { return m_Ticks != 0; }
			bool IsFinished() const noexcept { return m_Pass == Pass::Done; }

			// The scripted input of the next frame: steering, jumps and camera turns as key holds. It follows
			// the ticks played in this run, not frames, so loading frames never shift it; nothing is held
			// before a run's first tick.
			const InputState& NextInput()
			{
				const auto frame = m_TicksThisRun;
				const auto held = [frame](Key key) -> bool
				{
					if (frame == 0)
						return false;
					switch (key)
					{
					case Key::W: return frame % 240 < 150;
					case Key::A: return frame % 180 >= 60 && frame % 180 < 100;
					case Key::D: return frame % 300 >= 200 && frame % 300 < 260;
					case Key::Space: return frame % 90 < 4;
					case Key::Q: return frame % 400 >= 320 && frame % 400 < 350;
					default: return false;
					}
				};
				m_Input.Events.clear();
				for (const Key key : { Key::W, Key::A, Key::D, Key::Space, Key::Q })
				{
					auto& state = m_Input.Keys[static_cast<std::size_t>(key)];
					const bool down = held(key);
					state.Pressed = down && !state.Down;
					state.Released = !down && state.Down;
					state.Down = down;
					if (state.Pressed || state.Released)
						m_Input.Events.push_back({ InputCode::FromKey(key),
							state.Pressed ? InputTransition::Pressed : InputTransition::Released });
				}
				return m_Input;
			}

			// After each fixed step: records or compares the scene's hash.
			void AfterStep(const Scene& scene)
			{
				if (m_Pass == Pass::Done)
					return;
				++m_TicksThisRun;
				const auto bytes = CaptureScene(scene).Encode();
				std::uint64_t hash = 14695981039346656037ull;
				for (const auto value : bytes)
					hash = (hash ^ static_cast<std::uint8_t>(value)) * 1099511628211ull;
				if (m_Pass == Pass::Record)
				{
					m_Hashes.push_back(hash);
					m_Snapshots.push_back(bytes);
					if (m_Hashes.size() == m_Ticks)
					{
						m_Pass = Pass::Compare;
						m_Restart = true;
						Log(LogLevel::Info, std::format("Determinism check: recorded {} ticks (tick 2 hash {:016x}, last {:016x}); "
							"restarting the scene", m_Ticks, m_Hashes[std::min<std::size_t>(2, m_Hashes.size() - 1)], hash));
					}
					return;
				}
				if (hash != m_Hashes[m_Compared] && !m_Diverged)
				{
					// Both snapshots, for `RavenSceneTool dump`, beside the executable.
					m_Diverged = m_Compared;
					const auto directory = Paths::GetExecutableDirectory();
					Files::WriteAtomic(directory / "DeterminismFirstRun.ravenscene", m_Snapshots[m_Compared]);
					Files::WriteAtomic(directory / "DeterminismSecondRun.ravenscene", bytes);
				}
				if (++m_Compared == m_Ticks)
					m_Pass = Pass::Done;
			}

			// True once, when the second run should start from the scene file again.
			bool TakeRestart() noexcept { return std::exchange(m_Restart, false); }
			void OnRestart() noexcept { m_TicksThisRun = 0; }

			// After the loop: throws unless both runs matched at every tick.
			void Report() const
			{
				if (m_Pass != Pass::Done)
					throw std::runtime_error(std::format("Determinism check did not finish: the window closed after {} of {} ticks",
						m_Hashes.size() + m_Compared, std::size_t{ m_Ticks } * 2));
				if (m_Diverged)
					throw std::runtime_error(std::format("Determinism check failed: the runs first differ after tick {} of {}; "
						"both scenes are saved as DeterminismFirstRun/SecondRun.ravenscene beside the executable", *m_Diverged, m_Ticks));
				Log(LogLevel::Info, std::format("Determinism check passed: {} ticks matched, final hash {:016x}", m_Ticks,
					m_Hashes.back()));
			}

		private:
			enum class Pass : std::uint8_t { Record, Compare, Done };
			std::uint32_t m_Ticks = 0;
			Pass m_Pass = Pass::Record;
			std::vector<std::uint64_t> m_Hashes;
			std::vector<std::vector<std::byte>> m_Snapshots;
			std::size_t m_Compared = 0;
			std::optional<std::size_t> m_Diverged;
			bool m_Restart = false;
			std::uint64_t m_TicksThisRun = 0;
			InputState m_Input;
		};

		void DrawDeveloperOverlay(UI::Ui& ui, const UiPreview& preview, const std::vector<std::string>& entries)
		{
			const auto problems = GetRecentProblems(std::chrono::seconds(10));
			if (problems.empty() && preview.Entry < 0)
				return;
			UI::Style panel;
			panel.Absolute = true;
			// Bottom-left, clear of the corners games usually use for their HUD.
			panel.Start = 12.0f;
			panel.Bottom = 12.0f;
			panel.MaxWidth = 720.0f;
			panel.Padding = UI::All(10.0f);
			panel.Gap = 4.0f;
			panel.Background = UI::Rgba(0x101418E0);
			panel.CornerRadius = 6.0f;
			ui.Begin("Diagnostics", panel);
			UI::Style line;
			line.FontSize = 14.0f;
			if (preview.Entry >= 0)
			{
				line.TextColor = UI::Rgba(0x9AD0FFFF);
				const auto& name = entries[static_cast<std::size_t>(preview.Entry)];
				ui.Text("Preview", std::format("UI preview: {} ({}/{})  scale {:.2f}{}{}   F6 next, Shift+F6 scale, "
					"Ctrl+F6 direction, Alt+F6 pseudo-locale", name, preview.Entry + 1, entries.size(),
					UiPreview::Scales[preview.Scale], preview.RightToLeft ? "  right-to-left" : "",
					preview.Pseudo ? "  pseudo-locale" : ""), line);
			}
			for (std::size_t index = 0; index < problems.size(); ++index)
			{
				const auto& problem = problems[index];
				line.TextColor = problem.Level == LogLevel::Error ? UI::Rgba(0xFF8080FF) : UI::Rgba(0xFFD27AFF);
				ui.BeginKey("Problem", index + 1, {});
				ui.Text("Message", problem.Message, line);
				ui.End();
			}
			ui.End();
		}
	}

	void Application::Run(const WindowDesc& description, const ApplicationConfig& config)
	{
		if (!(config.MaxFrameRate >= 0.0) || !std::isfinite(config.MaxFrameRate))
			throw std::invalid_argument("MaxFrameRate must be zero or a positive finite rate");

		// The engine's shared compute workers (docs/JobSystemDesign.md); every user dies before them.
		JobSystem workers;
		// Engine content (shaders) stays beside the executable; the game's content is the session's.
		ApplicationPathsScope paths({});
		auto window = Window::Create(description);
		VulkanContext renderer(*window, config.Renderer);
		GameInstance instance;
		// Engine-owned mirror of the active scene's lit meshes; outlives the session and the module.
		RenderWorld renderWorld;
		StageBoundary boundary;
		std::unique_ptr<GameModule> game;
		if (!config.GameModulePath.empty())
			game = std::make_unique<GameModule>(instance, config.GameModulePath, config.AutoBuildGame);
		// The text stack (docs/UIDesign.md) and the UI contexts: the game's, which systems declare into,
		// and the developer overlay's. Engine content beside the executable holds the fonts and ICU data.
		Text::TextSystem text(renderer, workers, Paths::GetExecutableDirectory() / "Content");
		UI::UiContext gameUi(text);
		UI::UiContext developerUi(text);
		UiPreview preview;
		// The session dies before the module, the render world, the renderer and the window.
		GameSession session(renderer, instance, config);
		session.SetUi(&text, &gameUi, &developerUi);
		// The project's fixed-step rate, fixed for the session (docs/GameServicesDesign.md, FD-34).
		FixedGameplayStep gameplay(session.GetFixedStepRate());
		// Host-owned physics world of the active scene (FD-32). It dies before the registry it reads
		// and survives game module replacement.
		PhysicsWorld physics(session.GetPhysicsLimits(), &workers);
		session.SetPhysics(&physics);
		// Picking on Jolt against the static meshes as drawn (FD-34), synced only when a pick runs.
		PickWorld picking;
		session.SetPicking(&picking);
		std::vector<DebugLineVertex> debugLines;
		// Per-user data (docs/GameServicesDesign.md, FD-33), kept by the host across module reloads.
		std::optional<UserDataService> userData;
		if (!config.UserDataRoot.empty())
		{
			instance.GetRegistry().Register<InputBindingRecord>();
			userData.emplace(instance.GetRegistry(), config.UserDataRoot);
			userData->SetFormat(BindingsFile, UserDataFormat::Text);
			// Game saves are written after the frame, never inside a fixed step (AA2-GP-05).
			userData->SetDeferredSaves(true);
			session.SetUserData(&*userData);
			Log(LogLevel::Info, "User data folder: " + config.UserDataRoot.string());
		}
		// Saves user data on every exit, also while a fatal error unwinds. That is deliberate: FD-06
		// ends the process to avoid hiding corruption, but Save keeps a .bak, so at worst one file
		// is replaced by its backup. Declared after userData, so it runs before userData dies.
		struct SaveOnExit
		{
			std::optional<UserDataService>& Data;
			~SaveOnExit()
			{
				if (!Data)
					return;
				try
				{
					Data->SaveAll();
				}
				catch (const std::exception& error)
				{
					Log(LogLevel::Error, std::string("Saving user data on exit failed: ") + error.what());
				}
			}
		} saveOnExit{ userData };
		// Host-owned input mapping (docs/GameServicesDesign.md, FD-29); survives module reloads.
		// Saved bindings apply once the game's contexts exist, and changes are saved after the frame.
		InputRouter router(config.DeveloperInput);
		std::uint64_t routedGeneration = 0;
		bool bindingsLoaded = false;
		std::uint64_t savedBindings = 0;
		const auto syncGameInput = [&]
		{
			if (game && game->GetGeneration() != routedGeneration)
			{
				routedGeneration = game->GetGeneration();
				router.SetGame(game->GetInputContexts(), game->GetInputActions());
				// Record types may have changed with the module (FD-47).
				gameplay.DropRecords();
			}
			if (!bindingsLoaded && userData && (!game || routedGeneration != 0))
			{
				bindingsLoaded = true;
				LoadBindings(router, *userData);
				savedBindings = router.GetBindingRevision();
			}
		};
		const auto saveBindings = [&]
		{
			if (!userData || !bindingsLoaded || router.GetBindingRevision() == savedBindings)
				return;
			savedBindings = router.GetBindingRevision();
			StoreBindings(router, *userData);
			userData->Save(BindingsFile);
		};
		FrameClock clock;
		// Render interpolation between fixed ticks (FD-34).
		TransformInterpolator interpolator;
		auto droppedReport = std::chrono::steady_clock::now();
		double droppedSeconds = 0.0;
		bool wasDrawable = false;
		std::vector<FrameSample> samples;
		if (!config.FrameTracePath.empty())
			samples.reserve(32768);

		DeterminismCheck determinism(config.DeterminismTicks);
		while (!window->ShouldClose() && !determinism.IsFinished())
		{
			const auto frameStart = TraceClock::now();
			ActiveFrameMetrics telemetry(samples.size() < samples.capacity(), frameStart);
			auto* metrics = GetFrameMetrics();
			window->PollEvents();
			if (window->ShouldClose())
				break;
			if (game && config.WatchGameModule)
				game->Poll();
			syncGameInput();
			// The UI sees raw input first and withholds what it uses from gameplay (UIDesign.md section 2).
			// While the game listens for a key to bind, every key goes to the listener: the UI neither
			// withholds nor sees the keyboard, so Tab, Enter, Space and the arrows can be bound too.
			const bool listening = router.IsListening();
			const InputState& raw = determinism.IsActive() ? determinism.NextInput() : window->GetInputState();
			InputState routed = raw;
			if (!listening)
				gameUi.FilterInput(routed);
			router.Route(routed);
			InputState uiInput = raw;
			if (listening)
			{
				uiInput.Keys = {};
				std::erase_if(uiInput.Events, [](const InputEvent& event) { return event.Code.Device == InputDevice::Keyboard; });
			}
			// The game's view: raw input without what the developer and game contexts consumed.
			const InputState& input = router.GetGameInput();
			if (game && router.WasDeveloperPressed(DeveloperInput::ReloadGame))
			{
				game->Reload();
				syncGameInput();
			}

			const bool drawable = window->GetFramebufferState().IsDrawable();
			if (drawable)
			{
				FrameTime frameTime = clock.Tick();
				// The check plays exactly one fixed tick per frame, whatever the frame took.
				if (determinism.IsActive())
					frameTime.DeltaSeconds = gameplay.GetStepSeconds();
				bool replaced = false;
				// The UI preview mode: F6 steps through the game's entries and back to playing.
				const auto previewEntries = game ? game->GetPreviewNames() : std::vector<std::string>{};
				if (router.WasDeveloperPressed(DeveloperInput::PreviewNext))
				{
					preview.Entry = preview.Entry + 1 < static_cast<int>(previewEntries.size()) ? preview.Entry + 1 : -1;
					if (preview.Entry >= 0)
						Log(LogLevel::Info, std::format("UI preview: {} ({}/{})", previewEntries[static_cast<std::size_t>(preview.Entry)],
							preview.Entry + 1, previewEntries.size()));
					else
						Log(LogLevel::Info, previewEntries.empty() ? "UI preview: the game registers no entries" : "UI preview off");
				}
				if (preview.Entry >= static_cast<int>(previewEntries.size()))
					preview.Entry = -1;
				if (router.WasDeveloperPressed(DeveloperInput::PreviewScale))
					preview.Scale = (preview.Scale + 1) % std::size(UiPreview::Scales);
				if (router.WasDeveloperPressed(DeveloperInput::PreviewDirection))
					preview.RightToLeft = !preview.RightToLeft;
				if (router.WasDeveloperPressed(DeveloperInput::PreviewPseudo))
					preview.Pseudo = !preview.Pseudo;
				const bool previewing = preview.Entry >= 0;
				{
					const auto framebuffer = window->GetFramebufferState();
					const float dpi = window->GetContentScale();
					gameUi.SetRightToLeft(preview.RightToLeft);
					gameUi.SetPseudoLocalized(preview.Pseudo);
					gameUi.BeginFrame(uiInput, static_cast<float>(framebuffer.Width), static_cast<float>(framebuffer.Height),
						dpi * UiPreview::Scales[preview.Scale]);
					developerUi.BeginFrame(raw, static_cast<float>(framebuffer.Width), static_cast<float>(framebuffer.Height), dpi);
				}
				const DeveloperRequests requests{
					router.WasDeveloperPressed(DeveloperInput::RestartScene) || determinism.TakeRestart(),
					router.WasDeveloperPressed(DeveloperInput::NextScene),
					router.WasDeveloperPressed(DeveloperInput::CycleRenderView),
					router.WasDeveloperPressed(DeveloperInput::TogglePlay),
					router.WasDeveloperPressed(DeveloperInput::RestartPlay) };
				if (router.WasDeveloperPressed(DeveloperInput::ToggleColliders))
				{
					physics.SetColliderView(!physics.IsColliderViewEnabled());
					Log(LogLevel::Info, physics.IsColliderViewEnabled() ? "Collider view on" : "Collider view off");
				}
				// F9 while paused runs one fixed step: game systems, then physics (FD-32).
				const bool stepOnce = router.WasDeveloperPressed(DeveloperInput::StepFixed);
				// The developer quick-save (FD-33) works between frames, on the latest tick.
				const bool quickSave = router.WasDeveloperPressed(DeveloperInput::QuickSave);
				const bool quickLoad = router.WasDeveloperPressed(DeveloperInput::QuickLoad);
				std::optional<std::uint64_t> restoredTick;
				boundary.Run(StageBoundary::Prepare, [&]
				{
					replaced = session.Prepare(requests, frameTime);
					if (!replaced && quickSave)
						session.QuickSave(gameplay.GetTick());
					if (!replaced && quickLoad)
					{
						restoredTick = session.QuickLoad();
						replaced = restoredTick.has_value();
					}
				});
				if (metrics)
				{
					metrics->GameGeneration = game ? game->GetGeneration() : 0;
					metrics->DeltaSeconds = frameTime.DeltaSeconds;
					metrics->SceneReplaced = replaced;
				}
				FrameSection gameplayTime(metrics ? &metrics->Gameplay : nullptr);
				// Game phases run only while the session plays (docs/DataModelDesign.md, FD-12).
				// Deferred structural changes apply after each fixed step and after Update (FD-14).
				// Each fixed step: game systems, their commands, then physics (FD-32).
				const auto runStep = [&]
				{
					const FrameTime step = gameplay.GetStepTime();
					gameplay.BeginStep();
					interpolator.BeforeStep(session.GetScene());
					physics.SetFixedPhase(true);
					if (game)
					{
						ActionView actions(router, gameplay.GetActions());
						auto context = session.MakeContext(gameplay.GetInput(), actions, gameplay, step);
						game->FixedUpdate(context);
					}
					physics.SetFixedPhase(false);
					session.ApplyCommands();
					{
						FrameSection physicsTime(metrics ? &metrics->Physics : nullptr);
						physics.Step(session.GetScene(), instance.GetRegistry(), gameplay.GetStepSeconds());
					}
					gameplay.ConsumeStep();
					if (determinism.IsActive())
						determinism.AfterStep(session.GetScene());
				};
				const bool played = boundary.Run(StageBoundary::Gameplay, [&]
				{
					if (replaced)
					{
						physics.Clear();
						picking.Clear();
					}
					if (replaced || !wasDrawable || !session.IsPlaying() || previewing)
					{
						gameplay.Reset();
						interpolator.Snap();
						if (replaced)
						{
							determinism.OnRestart();
							clock.ResetDelta();
							gameplay.RestartTicks(restoredTick.value_or(0));
							interpolator.Clear();
						}
						else if (stepOnce && !session.IsPlaying())
						{
							gameplay.BeginFrame(input, router.GetGameActions(), gameplay.GetStepSeconds());
							if (gameplay.HasStep())
							{
								runStep();
								interpolator.AfterSteps(session.GetScene());
								interpolator.Snap();
								Log(LogLevel::Info, std::format("Stepped one fixed step ({} physics bodies)",
									physics.GetStats().Bodies));
							}
							gameplay.Reset();
						}
					}
					else
					{
						// Controllers run before the steps, so their records reach this frame's
						// first step (FD-47).
						if (game)
						{
							ActionView actions(router, router.GetGameActions());
							auto context = session.MakeContext(input, actions, gameplay, frameTime);
							game->Control(context);
						}
						// A game pause (a pause menu) stops the fixed steps and physics, not the frame.
						if (session.IsSimulationPaused())
						{
							gameplay.Reset();
							interpolator.Snap();
						}
						else
						{
							gameplay.BeginFrame(input, router.GetGameActions(), frameTime.DeltaSeconds);
							const bool stepped = gameplay.HasStep();
							// No tick runs on a scene the game has asked to leave: a scene request
							// drops the frame's remaining steps, and the next Prepare applies it.
							while (gameplay.HasStep() && !session.HasSceneRequest())
								runStep();
							if (session.HasSceneRequest())
								gameplay.Reset();
							if (stepped)
								interpolator.AfterSteps(session.GetScene());
						}
					}
					if (game && session.IsPlaying() && !previewing)
					{
						ActionView actions(router, router.GetGameActions());
						auto context = session.MakeContext(input, actions, gameplay, frameTime);
						game->Update(context);
					}
					if (previewing)
						game->RunPreview(static_cast<std::size_t>(preview.Entry), gameUi);
					session.ApplyCommands();
				});
				// A failed step is not retried: its remaining steps are dropped with it.
				if (!played)
					gameplay.Reset();
				gameplayTime.End();
				if (metrics)
				{
					metrics->BenchmarkPhase = session.GetTracePhase();
					const auto stats = physics.GetStats();
					metrics->PhysicsBodies = stats.Bodies;
					metrics->PhysicsActive = stats.ActiveBodies;
					metrics->PhysicsContacts = stats.Contacts;
				}

				// Engine PostUpdate and Extract: cached world transforms, then the render world.
				// Interpolated entities (FD-34) are drawn between their last two ticks: their blended
				// transforms go through the world pass, the render world and the sprites, and the
				// latest tick is restored after drawing.
				bool blended = false;
				{
					auto& scene = session.GetScene();
					const auto& catalog = session.GetCatalog();
					FrameSection gather(metrics ? &metrics->SceneGather : nullptr);
					boundary.Run(StageBoundary::PostUpdate, [&]
					{
						FrameSection transforms(metrics ? &metrics->WorldTransforms : nullptr);
						if (session.IsPlaying() && interpolator.GetCount() > 0)
							blended = interpolator.Apply(scene, static_cast<float>(gameplay.GetAlpha()));
						scene.UpdateWorldTransforms();
					});
					// Only new objects and changed materials resolve their texture (G1), and only new
					// objects and changed mesh components their mesh. A bad game-written texture
					// samples white (bindless element 0); a bad mesh draws nothing (FD-06).
					boundary.Run(StageBoundary::Extract, [&]
					{
						renderWorld.Sync(scene, [&](ECS::Entity, const ECS::MaterialComponent& material)
							-> const VulkanTexture*
						{
							if (!material.BaseColorTexture.IsValid())
								return nullptr;
							const auto* lease = scene.TryUseAsset(catalog, material.BaseColorTexture.Id, AssetKind::Texture);
							return lease ? &lease->GetTexture() : nullptr;
						}, [&](ECS::Entity, AssetRef<MeshAsset> mesh) -> MeshHandle
						{
							if (!mesh.IsValid())
								return {};
							const auto* lease = scene.TryUseAsset(catalog, mesh.Id, AssetKind::Mesh);
							return lease ? lease->GetMesh() : MeshHandle{};
						});
					});
				}
				boundary.Run(StageBoundary::Render, [&]
				{
					DrawDeveloperOverlay(developerUi, preview, previewEntries);
					debugLines.clear();
					physics.DrawColliders(debugLines);
					session.Render(renderer, renderWorld, input, debugLines);
				});
				// Runs whether or not Render succeeded, so blended transforms never stay authoritative.
				if (blended)
					boundary.Run(StageBoundary::PostUpdate, [&]
					{
						auto& scene = session.GetScene();
						interpolator.Restore(scene);
						scene.UpdateWorldTransforms();
					});
			}
			else
			{
				gameplay.Reset();
				clock.ResetDelta();
			}
			wasDrawable = drawable;
			saveBindings();
			if (userData)
				userData->FlushSaves();
			// Dropped game time is reported at most once a second (FD-34).
			droppedSeconds += gameplay.TakeDroppedSeconds();
			if (droppedSeconds > 0.0 && std::chrono::steady_clock::now() - droppedReport >= std::chrono::seconds(1))
			{
				Log(LogLevel::Warning, std::format("Fixed step dropped {:.1f} ms of game time: frames took longer "
					"than the catch-up limit", droppedSeconds * 1000.0));
				droppedSeconds = 0.0;
				droppedReport = std::chrono::steady_clock::now();
			}
			if (metrics)
				samples.push_back({ *metrics, TraceNanoseconds(TraceClock::now() - frameStart), drawable });

			// FIFO presentation paces drawable frames, so only an explicit cap sleeps for them.
			// A minimized window has nothing to present and is throttled to 30 frames per second.
			using Seconds = std::chrono::duration<double>;
			const double rate = drawable ? config.MaxFrameRate : 30.0;
			if (rate > 0.0)
				std::this_thread::sleep_until(frameStart + std::chrono::duration_cast<TraceClock::duration>(Seconds(1.0 / rate)));
		}
		if (!config.FrameTracePath.empty())
		{
			std::ofstream trace(config.FrameTracePath);
			trace << "start_ns,work_ns,drawable,gameplay_ns,scene_gather_ns,world_ns,render_prepare_ns,record_submit_ns,"
				"fence_wait_ns,acquire_wait_ns,present_wait_ns,presented_ns,gpu_frame_ns,gpu_ns,gpu_cull_ns,visible_meshes,draw_calls,"
				"game_generation,delta_seconds,benchmark_phase,scene_replaced,physics_ns,physics_bodies,physics_active,"
				"physics_contacts,ui_ns,ui_quads\n" << std::setprecision(10);
			for (const auto& sample : samples)
			{
				const auto& m = sample.Metrics;
				trace << m.Start << ',' << sample.Work << ',' << sample.Drawable << ',' << m.Gameplay
					<< ',' << m.SceneGather << ',' << m.WorldTransforms << ',' << m.RenderPrepare << ',' << m.RecordSubmit
					<< ',' << m.FenceWait << ',' << m.AcquireWait << ',' << m.PresentWait << ',' << m.Presented
					<< ',' << m.GpuFrame << ',' << m.Gpu << ',' << m.GpuCull << ',' << m.VisibleMeshes << ',' << m.DrawCalls
					<< ',' << m.GameGeneration << ',' << m.DeltaSeconds << ',' << m.BenchmarkPhase << ',' << m.SceneReplaced
					<< ',' << m.Physics << ',' << m.PhysicsBodies << ',' << m.PhysicsActive << ',' << m.PhysicsContacts
					<< ',' << m.Ui << ',' << m.UiQuads << '\n';
			}
			trace.close();
			if (!trace)
				throw std::runtime_error("Cannot write frame timing capture");
			if (game)
			{
				// Per-system host time beside the frame trace, accumulated across reloads.
				auto systemsPath = config.FrameTracePath;
				systemsPath.replace_extension(".systems.csv");
				std::ofstream systems(systemsPath);
				systems << "system,phase,calls,total_ns,mean_ns,suspended\n";
				for (const auto& stats : game->GetSystemStats())
					systems << stats.Name << ',' << (stats.Phase == SystemPhase::FixedUpdate ? "fixed" : stats.Phase == SystemPhase::Control ? "control" : "update") << ','
						<< stats.Calls << ',' << stats.Nanoseconds << ','
						<< (stats.Calls ? stats.Nanoseconds / stats.Calls : 0) << ',' << stats.Suspended << '\n';
				systems.close();
				if (!systems)
					throw std::runtime_error("Cannot write system timing capture");
			}
			// Every watched input's dependency record (docs/HotReloadDesign.md, H1 and H7).
			auto reloadsPath = config.FrameTracePath;
			reloadsPath.replace_extension(".reloads.csv");
			std::ofstream reloads(reloadsPath);
			const auto quote = [](const std::string& text)
			{
				std::string quoted = "\"";
				for (const char value : text)
					quoted += value == '"' ? std::string("\"\"") : std::string(1, value);
				return quoted + '"';
			};
			reloads << "name,importer_version,inputs,imports,failures,superseded,last_import_ms,last_publish_ms,trigger,output,last_error\n";
			for (const auto& record : ReloadService::GetRecords())
			{
				std::string trigger;
				for (const auto& path : record.Trigger)
					trigger += (trigger.empty() ? "" : ";") + path.filename().string();
				reloads << quote(record.Name) << ',' << record.ImporterVersion << ',' << record.Inputs.size() << ','
					<< record.Imports << ',' << record.Failures << ',' << record.Superseded << ','
					<< record.LastImport.count() << ',' << record.LastPublish.count() << ',' << quote(trigger) << ','
					<< quote(record.Output) << ',' << quote(record.LastError) << '\n';
			}
			reloads.close();
			if (!reloads)
				throw std::runtime_error("Cannot write reload record capture");
			Log(LogLevel::Info, "Reload service: " + std::to_string(ReloadService::GetRecords().size()) +
				" watched consumers on " + std::to_string(ReloadService::GetThreadCount()) + " threads");
		}
		if (determinism.IsActive())
			determinism.Report();
	}
}

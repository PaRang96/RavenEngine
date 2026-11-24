#include "Raven/Core/Application.hpp"
#include "Core/ReloadService.hpp"
#include "Core/FrameClock.hpp"
#include "Core/FrameTrace.hpp"
#include "Core/FixedGameplayStep.hpp"
#include "Core/GameSession.hpp"
#include "Core/Paths.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/GameModule.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Renderer/RenderWorld.hpp"
#include "Raven/Scene/Scene.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <new>
#include <optional>
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
		class StageBoundary
		{
		public:
			enum Stage : std::size_t { Prepare, Gameplay, PostUpdate, Extract, Count };

			template<typename Function>
			bool Run(Stage stage, Function&& function)
			{
				auto& state = m_States[stage];
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
				catch (const std::exception& error)
				{
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
				if (!state.Message.empty())
				{
					Log(LogLevel::Info, std::string(Names[stage]) + " recovered after " +
						std::to_string(state.Repeats + 1) + " failed frame(s)");
					state = {};
				}
				return true;
			}

		private:
			static constexpr const char* Names[Count]{ "Scene preparation", "Gameplay",
				"World transform update", "Render world sync" };
			struct State
			{
				std::string Message;
				std::uint64_t Repeats = 0;
			};
			State m_States[Count];
		};
	}

	FrameMetrics* GetFrameMetrics() noexcept { return CurrentMetrics; }

	void Application::Run(const WindowDesc& description, const ApplicationConfig& config)
	{
		if (!(config.MaxFrameRate >= 0.0) || !std::isfinite(config.MaxFrameRate))
			throw std::invalid_argument("MaxFrameRate must be zero or a positive finite rate");

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
		// The session dies before the module, the render world, the renderer and the window.
		GameSession session(renderer, instance, config);
		FrameClock clock;
		FixedGameplayStep gameplay;
		bool wasDrawable = false;
		std::vector<FrameSample> samples;
		if (!config.FrameTracePath.empty())
			samples.reserve(32768);

		while (!window->ShouldClose())
		{
			const auto frameStart = TraceClock::now();
			ActiveFrameMetrics telemetry(samples.size() < samples.capacity(), frameStart);
			auto* metrics = GetFrameMetrics();
			window->PollEvents();
			if (window->ShouldClose())
				break;
			const InputState& input = window->GetInputState();
			if (game)
			{
				if (config.WatchGameModule)
					game->Poll();
				if (input.WasKeyPressed(Key::F5))
					game->Reload();
			}

			const bool drawable = window->GetFramebufferState().IsDrawable();
			if (drawable)
			{
				const FrameTime frameTime = clock.Tick();
				bool replaced = false;
				boundary.Run(StageBoundary::Prepare, [&] { replaced = session.Prepare(input, frameTime); });
				if (metrics)
				{
					metrics->GameGeneration = game ? game->GetGeneration() : 0;
					metrics->DeltaSeconds = frameTime.DeltaSeconds;
					metrics->SceneReplaced = replaced;
				}
				FrameSection gameplayTime(metrics ? &metrics->Gameplay : nullptr);
				// Game phases run only while the session plays (docs/DataModelDesign.md, FD-12).
				// Deferred structural changes apply after each fixed step and after Update (FD-14).
				const bool played = boundary.Run(StageBoundary::Gameplay, [&]
				{
					if (replaced || !wasDrawable || !session.IsPlaying())
					{
						gameplay.Reset();
						if (replaced)
							clock.ResetDelta();
					}
					else
					{
						gameplay.BeginFrame(input, frameTime.DeltaSeconds);
						while (gameplay.HasStep())
						{
							const FrameTime step = gameplay.GetStepTime();
							if (game)
							{
								auto context = session.MakeContext(gameplay.GetInput(), step);
								game->FixedUpdate(context);
							}
							session.ApplyCommands();
							gameplay.ConsumeStep();
						}
					}
					if (game && session.IsPlaying())
					{
						auto context = session.MakeContext(input, frameTime);
						game->Update(context);
					}
					session.ApplyCommands();
				});
				// A failed step is not retried: its remaining steps are dropped with it.
				if (!played)
					gameplay.Reset();
				gameplayTime.End();
				if (metrics)
					metrics->BenchmarkPhase = session.GetTracePhase();

				// Engine PostUpdate and Extract: cached world transforms, then the render world.
				{
					auto& scene = session.GetScene();
					const auto& catalog = session.GetCatalog();
					FrameSection gather(metrics ? &metrics->SceneGather : nullptr);
					boundary.Run(StageBoundary::PostUpdate, [&]
					{
						FrameSection transforms(metrics ? &metrics->WorldTransforms : nullptr);
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
				session.Render(renderer, renderWorld, input);
			}
			else
			{
				gameplay.Reset();
				clock.ResetDelta();
			}
			wasDrawable = drawable;
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
				"game_generation,delta_seconds,benchmark_phase,scene_replaced\n" << std::setprecision(10);
			for (const auto& sample : samples)
			{
				const auto& m = sample.Metrics;
				trace << m.Start << ',' << sample.Work << ',' << sample.Drawable << ',' << m.Gameplay
					<< ',' << m.SceneGather << ',' << m.WorldTransforms << ',' << m.RenderPrepare << ',' << m.RecordSubmit
					<< ',' << m.FenceWait << ',' << m.AcquireWait << ',' << m.PresentWait << ',' << m.Presented
					<< ',' << m.GpuFrame << ',' << m.Gpu << ',' << m.GpuCull << ',' << m.VisibleMeshes << ',' << m.DrawCalls
					<< ',' << m.GameGeneration << ',' << m.DeltaSeconds << ',' << m.BenchmarkPhase << ',' << m.SceneReplaced << '\n';
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
					systems << stats.Name << ',' << (stats.Phase == SystemPhase::FixedUpdate ? "fixed" : "update") << ','
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
	}
}

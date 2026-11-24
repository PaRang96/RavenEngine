#include "Raven/Core/Application.hpp"
#include "Core/ReloadService.hpp"
#include "Core/FrameClock.hpp"
#include "Core/FrameTrace.hpp"
#include "Core/FixedGameplayStep.hpp"
#include "Core/Paths.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/GameModule.hpp"
#include "Raven/Core/Log.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <stdexcept>
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
	}

	FrameMetrics* GetFrameMetrics() noexcept { return CurrentMetrics; }

	void Application::Run(const WindowDesc& description, const ApplicationClientFactory& factory)
	{
		Run(description, factory, ApplicationConfig{});
	}

	void Application::Run(const WindowDesc& description, const ApplicationClientFactory& factory,
		const ApplicationConfig& config)
	{
		if ((!factory.Create && !factory.CreateGameClient) || !factory.Destroy)
			throw std::invalid_argument("Application requires client create and destroy callbacks");
		if (!(config.MaxFrameRate >= 0.0) || !std::isfinite(config.MaxFrameRate))
			throw std::invalid_argument("MaxFrameRate must be zero or a positive finite rate");

		ApplicationPathsScope paths(config.ContentRoot);
		auto window = Window::Create(description);
		VulkanContext renderer(*window, config.Renderer);
		GameInstance instance;
		std::unique_ptr<GameModule> game;
		if (!config.GameModulePath.empty())
			game = std::make_unique<GameModule>(config.GameModulePath, config.AutoBuildGame);
		// The retained host client dies before module, session, renderer and window.
		auto* createdClient = factory.CreateGameClient
			? factory.CreateGameClient(renderer, instance)
			: factory.Create(renderer);
		std::unique_ptr<ApplicationClient, decltype(factory.Destroy)> client(createdClient, factory.Destroy);
		if (!client)
			throw std::runtime_error("Application client factory returned null");
		FrameClock clock;
		FixedGameplayStep gameplay;
		const bool fixedGameplay = client->UsesFixedGameplayStep();
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
				const bool replaced = client->PrepareFrame(input, frameTime);
				if (metrics)
				{
					metrics->GameGeneration = game ? game->GetGeneration() : 0;
					metrics->DeltaSeconds = frameTime.DeltaSeconds;
					metrics->SceneReplaced = replaced;
				}
				FrameSection gameplayTime(metrics ? &metrics->Gameplay : nullptr);
				if (fixedGameplay)
				{
					if (replaced || !wasDrawable)
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
							GameUpdateResult result;
							const FrameTime step = gameplay.GetStepTime();
							if (game)
								result = game->FixedUpdate(instance, gameplay.GetInput(), step, client->GetGameBindings());
							client->OnGameplayStep(result);
							gameplay.ConsumeStep();
						}
					}
				}
				// Variable-rate systems run once per drawable frame, after any fixed steps.
				if (game)
					game->Update(instance, input, frameTime, client->GetGameBindings());

				gameplayTime.End();
				client->OnFrame(renderer, input, frameTime);
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

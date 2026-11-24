#include "OrthographicSample.hpp"
#include "PerspectiveSample.hpp"
#include "GameBuildConfig.hpp"

#include "Raven/Core/Application.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Window.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
	using namespace Raven;

	void DestroySample(ApplicationClient* client) noexcept
	{
		delete client;
	}

	struct SampleOptions
	{
		bool FreeFly = false;
		bool WatchGame = false;
		bool WatchMesh = false;
		bool WatchWall = false;
		bool WatchShader = false;
		std::optional<AssetId> StartScene;
	};

	SampleOptions Options;

	// Presentation and placement options. They do not select a sample mode.
	struct PresentationOptions
	{
		RendererConfig Renderer;
		double MaxFrameRate = 0.0;
		std::optional<WindowPosition> Position;
		std::optional<std::pair<std::uint32_t, std::uint32_t>> BorderlessSize;
	};

	long long ParseInteger(std::string_view text, const char* option)
	{
		long long value = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (error != std::errc{} || end != text.data() + text.size())
			throw std::invalid_argument(std::string(option) + " expects whole numbers");
		return value;
	}

	// Returns false when `option` is not a presentation option.
	bool ParsePresentationOption(std::string_view option, int argc, char* argv[], int& index, PresentationOptions& result)
	{
		const auto next = [&](const char* expected) -> std::string_view
		{
			if (++index == argc)
				throw std::invalid_argument(std::string(option) + " expects " + expected);
			return argv[index];
		};
		if (option == "--present")
		{
			const auto mode = next("fifo, mailbox or immediate");
			if (mode == "fifo")
				result.Renderer.Present = PresentMode::Fifo;
			else if (mode == "mailbox")
				result.Renderer.Present = PresentMode::Mailbox;
			else if (mode == "immediate")
				result.Renderer.Present = PresentMode::Immediate;
			else
				throw std::invalid_argument("--present expects fifo, mailbox or immediate");
		}
		else if (option == "--max-fps")
		{
			const auto rate = ParseInteger(next("a frame rate (0 for none)"), "--max-fps");
			if (rate < 0 || rate > 10000)
				throw std::invalid_argument("--max-fps expects 0 to 10000");
			result.MaxFrameRate = static_cast<double>(rate);
		}
		else if (option == "--gpu")
		{
			const auto type = next("discrete or integrated");
			if (type == "discrete")
				result.Renderer.Gpu = GpuPreference::Discrete;
			else if (type == "integrated")
				result.Renderer.Gpu = GpuPreference::Integrated;
			else
				throw std::invalid_argument("--gpu expects discrete or integrated");
		}
		else if (option == "--borderless")
		{
			const auto x = ParseInteger(next("<x> <y> <width> <height>"), "--borderless");
			const auto y = ParseInteger(next("<x> <y> <width> <height>"), "--borderless");
			const auto width = ParseInteger(next("<x> <y> <width> <height>"), "--borderless");
			const auto height = ParseInteger(next("<x> <y> <width> <height>"), "--borderless");
			constexpr long long Limit = 1 << 16;
			if (x < -Limit || x > Limit || y < -Limit || y > Limit || width <= 0 || width > Limit || height <= 0 || height > Limit)
				throw std::invalid_argument("--borderless expects a position and a positive size within 65536 pixels");
			result.Position = WindowPosition{ static_cast<std::int32_t>(x), static_cast<std::int32_t>(y) };
			result.BorderlessSize.emplace(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
		}
		else
			return false;
		return true;
	}

	ApplicationClient* CreatePerspectiveSample(VulkanContext& renderer, GameInstance& instance)
	{
		if (Options.WatchShader)
			renderer.WatchShaders();
		return new PerspectiveSample(renderer, instance, Options.FreeFly, Options.WatchMesh, Options.WatchGame, Options.StartScene);
	}

	ApplicationClient* CreateOrthographicSample(VulkanContext& renderer, GameInstance& instance)
	{
		if (Options.WatchShader)
			renderer.WatchShaders();
		return new OrthographicSample(renderer, instance, Options.WatchWall, Options.WatchGame);
	}
}

int main(int argc, char* argv[])
{
	try
	{
		const auto executableDirectory = Raven::Paths::GetExecutableDirectory();
		const bool playerBuild = std::filesystem::is_regular_file(executableDirectory / "RavenPlayer.ravenlaunch");
		std::optional<bool> selectedOrthographic;
		bool loadGame = false;
		bool noWatch = playerBuild;
		bool defaultMode = true;
		PresentationOptions presentation;
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
			if (ParsePresentationOption(option, argc, argv, index, presentation))
				continue;
			if (option == "--scene")
			{
				if (++index == argc || Options.StartScene)
					throw std::invalid_argument("--scene expects one Perspective scene UUID");
				Options.StartScene = AssetId::Parse(argv[index]);
				continue;
			}
			defaultMode = false;
			if (option == "--orthographic")
				selectedOrthographic = true;
			else if (option == "--perspective")
				selectedOrthographic = false;
			else if (option == "--game")
				loadGame = true;
			else if (option == "--free-fly")
				Options.FreeFly = true;
			else if (option == "--watch-game")
				Options.WatchGame = true;
			else if (option == "--watch-wall")
				Options.WatchWall = true;
			else if (option == "--watch-shader")
				Options.WatchShader = true;
			else if (option == "--watch-mesh")
				Options.WatchMesh = true;
			else if (option == "--no-watch")
				noWatch = true;
			else
				throw std::invalid_argument("Usage: RavenEngine [--perspective | --orthographic] [--game | --watch-game] [--free-fly] [--watch-wall] [--watch-shader] [--watch-mesh] [--no-watch] [--scene <UUID>] [--present fifo|mailbox|immediate] [--max-fps <rate>] [--gpu discrete|integrated] [--borderless <x> <y> <width> <height>]");
		}
		if (noWatch && (Options.WatchGame || Options.WatchMesh || Options.WatchWall || Options.WatchShader))
			throw std::invalid_argument("--no-watch cannot be combined with --watch options");
		if (GameBuild::Enabled && defaultMode && !playerBuild)
		{
			Options.FreeFly = true;
			Options.WatchGame = true;
			Options.WatchMesh = true;
			Options.WatchShader = true;
		}
		const bool orthographic = selectedOrthographic.value_or(!Options.FreeFly && !Options.WatchMesh &&
			(loadGame || Options.WatchGame || Options.WatchWall || Options.WatchShader));
		if (!orthographic && (loadGame || Options.WatchGame || (noWatch && GameBuild::Enabled)))
			Options.FreeFly = true;
		const bool gameEnabled = loadGame || Options.WatchGame || Options.FreeFly;
		if (Options.StartScene && orthographic)
			throw std::invalid_argument("--scene currently selects a Perspective scene");
		if (Options.FreeFly && orthographic)
			throw std::invalid_argument("--free-fly requires the Perspective sample");
		if (Options.WatchWall && !orthographic)
			throw std::invalid_argument("--watch-wall requires the Orthographic sample");
		if (Options.WatchMesh && orthographic)
			throw std::invalid_argument("--watch-mesh requires the Perspective sample");
		Raven::WindowDesc description;
		Raven::ApplicationClientFactory factory;
		factory.Destroy = DestroySample;

		const char* controls;
		if (orthographic)
		{
			factory.CreateGameClient = CreateOrthographicSample;
			description.Title = "RavenEngine | Orthographic 2D";
			controls = "Orthographic sample: arrows pan, wheel/PageUp/PageDown zoom, Home camera reset, F1 order, F2 camera, F3 restart, F4 level. Courtyard with --game or --watch-game: WASD move to the green beacon, avoid the gold courier, and go around the purple wall.";
		}
		else
		{
			factory.CreateGameClient = CreatePerspectiveSample;
			if (Options.FreeFly)
			{
				description.Title = playerBuild ? "RavenEngine" : "RavenEngine | Perspective free-fly | F5 reload";
				controls = "Perspective free-fly: fly to the cyan beacon, F3 restart, F4 switch authored level, WASD move, Q/E descend/ascend, right-drag look, wheel FOV, left-click ground hit, hold T to rotate the bound mesh, F6 swap mesh sources";
			}
			else
			{
				description.Title = "RavenEngine | Transparency | Arrows orbit, wheel zoom, Home/R reset, Space pause, F1/O order";
				controls = "Perspective sample: arrows orbit, wheel zoom, Home/R reset, Space pause, F1/O transparent order, F4 switch level, F6 swap mesh sources";
			}
		}

		Raven::ApplicationConfig config;
		config.WatchGameModule = !noWatch;
		config.Renderer = presentation.Renderer;
		config.MaxFrameRate = presentation.MaxFrameRate;
		if (presentation.BorderlessSize)
		{
			description.Position = presentation.Position;
			description.Borderless = true;
			description.Width = presentation.BorderlessSize->first;
			description.Height = presentation.BorderlessSize->second;
		}
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
		if (const char* trace = std::getenv("RAVEN_FRAME_TRACE"); trace && *trace)
			config.FrameTracePath = trace;
		if (gameEnabled)
		{
			config.GameModulePath = executableDirectory / GameBuild::ModuleFilename;
			config.AutoBuildGame = Options.WatchGame;
			if (!Options.FreeFly)
				description.Title = "RavenEngine | ECS + Game reload | F5 reload";
			if (Options.WatchGame)
				Raven::Log(Raven::LogLevel::Info, "Game source watching enabled; edit Private/Game/RavenGame.cpp. F5 reloads the built DLL.");
			else if (noWatch)
				Raven::Log(Raven::LogLevel::Info, "Source and DLL watching disabled; using staged content.");
			else
				Raven::Log(Raven::LogLevel::Info, "Game reload sample: build the standalone GameBuild project while running; F5 reloads the DLL.");
		}
		Raven::Log(Raven::LogLevel::Info, controls);
		Raven::Application app;
		app.Run(description, factory, config);

		return 0;
	}
	catch (const std::exception& error)
	{
		Raven::Log(Raven::LogLevel::Error, error.what());
		return 1;
	}
}

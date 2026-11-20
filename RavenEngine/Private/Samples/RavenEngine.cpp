#include "OrthographicSample.hpp"
#include "PerspectiveSample.hpp"
#include "GameBuildConfig.hpp"

#include "Raven/Core/Application.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Window.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

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

	ApplicationClient* CreatePerspectiveSample(VulkanContext& renderer, GameInstance& instance)
	{
		if (Options.WatchShader)
			renderer.WatchLitFragment();
		return new PerspectiveSample(renderer, instance, Options.FreeFly, Options.WatchMesh, Options.WatchGame, Options.StartScene);
	}

	ApplicationClient* CreateOrthographicSample(VulkanContext& renderer, GameInstance& instance)
	{
		if (Options.WatchShader)
			renderer.WatchTexturedFragment();
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
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
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
				throw std::invalid_argument("Usage: RavenEngine [--perspective | --orthographic] [--game | --watch-game] [--free-fly] [--watch-wall] [--watch-shader] [--watch-mesh] [--no-watch] [--scene <UUID>]");
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

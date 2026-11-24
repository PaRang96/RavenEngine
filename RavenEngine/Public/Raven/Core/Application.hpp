#pragma once

#include "Raven/Assets/AssetId.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Renderer/RendererConfig.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Raven
{
	struct WindowDesc;

	// One game root (docs/DataModelDesign.md, FD-17): its content, its module and the options it
	// is started with. The engine owns the frame, the scene session and the render world.
	struct ApplicationConfig
	{
		// The game's content folder, holding Project.ravenproject. Relative paths use the
		// executable's directory.
		std::filesystem::path ContentRoot;
		// Empty runs without game code. Paths must name trusted local code.
		std::filesystem::path GameModulePath;
		bool AutoBuildGame = false;
		bool WatchGameModule = true;
		// Development: reload the project, scenes, meshes and textures, and every engine shader.
		bool WatchContent = false;
		bool WatchShaders = false;
		std::optional<AssetId> StartScene;
		// `--name` options the engine does not use itself, for GameServices::HasOption.
		std::vector<std::string> GameOptions;
		// Optional buffered timing capture, written after the run.
		std::filesystem::path FrameTracePath;
		RendererConfig Renderer;
		// Frame-rate cap for drawable frames; 0 means none, because FIFO presentation already
		// paces to the display. Mailbox and Immediate run uncapped unless this is set.
		double MaxFrameRate = 0.0;
	};

	class Application
	{
	public:
		Application() = default;
		~Application() = default;

		RAVEN_API void Run(const WindowDesc& description, const ApplicationConfig& config);
	};
}

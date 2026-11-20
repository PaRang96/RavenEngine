#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Core/ApplicationClient.hpp"

#include <filesystem>

namespace Raven
{
	struct WindowDesc;

	struct ApplicationConfig
	{
		// Empty uses Content beside the executable. Relative roots use that same directory.
		std::filesystem::path ContentRoot;
		// Empty disables development game-module loading. Paths must name trusted local code.
		std::filesystem::path GameModulePath;
		bool AutoBuildGame = false;
		bool WatchGameModule = true;
		// Optional buffered timing capture, written after the run.
		std::filesystem::path FrameTracePath;
	};

	class Application
	{
	public:
		Application() = default;
		~Application() = default;

		RAVEN_API void Run(const WindowDesc& description, const ApplicationClientFactory& factory);
		RAVEN_API void Run(const WindowDesc& description, const ApplicationClientFactory& factory,
			const ApplicationConfig& config);
	};
}

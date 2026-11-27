// The generic engine host (docs/DataModelDesign.md, FD-17): runs one game root from Games/<Name>
// with its module and content. Every game behavior lives in the game's module.

#include "GameBuildConfig.hpp"

#include "Raven/Core/Application.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Window.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
	using namespace Raven;

	// Presentation and placement options.
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

	// For unsigned options: no sign, and nothing above `max`, so every logged seed reads back.
	std::uint64_t ParseUnsigned(std::string_view text, const char* option, std::uint64_t max)
	{
		std::uint64_t value = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (error != std::errc{} || end != text.data() + text.size() || value > max)
			throw std::invalid_argument(std::format("{} expects 0 to {}", option, max));
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

	constexpr std::string_view Usage = "Usage: RavenEngine [--game <Name>] [--scene <UUID>] [--no-watch] [--seed <n>] "
		"[--present fifo|mailbox|immediate] [--max-fps <rate>] [--gpu discrete|integrated] "
		"[--borderless <x> <y> <width> <height>] [--<game option>...]";
}

int main(int argc, char* argv[])
{
	try
	{
		const auto executableDirectory = Paths::GetExecutableDirectory();
		const bool playerBuild = std::filesystem::is_regular_file(executableDirectory / "RavenPlayer.ravenlaunch");
		std::string game = GameBuild::DefaultGame;
		bool noWatch = playerBuild;
		PresentationOptions presentation;
		ApplicationConfig config;
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
			if (ParsePresentationOption(option, argc, argv, index, presentation))
				continue;
			if (option == "--scene")
			{
				if (++index == argc || config.StartScene)
					throw std::invalid_argument("--scene expects one scene UUID");
				config.StartScene = AssetId::Parse(argv[index]);
			}
			else if (option == "--game")
			{
				if (++index == argc || playerBuild)
					throw std::invalid_argument(playerBuild ? "A player runs its packaged game" : "--game expects a game name");
				game = argv[index];
			}
			else if (option == "--no-watch")
				noWatch = true;
			// The simulation seed (docs/GameServicesDesign.md, FD-48), for reproducing a run.
			else if (option == "--seed")
			{
				if (++index == argc)
					throw std::invalid_argument("--seed expects a whole number");
				config.SimulationSeed = ParseUnsigned(argv[index], "--seed", std::numeric_limits<std::uint64_t>::max());
			}
			// The determinism check (FD-48): `--determinism-check [ticks]`, 600 ticks by default.
			else if (option == "--determinism-check")
			{
				config.DeterminismTicks = 600;
				if (index + 1 < argc && std::string_view(argv[index + 1]).find_first_not_of("0123456789") == std::string_view::npos)
					config.DeterminismTicks = static_cast<std::uint32_t>(ParseUnsigned(argv[++index], "--determinism-check",
						std::numeric_limits<std::uint32_t>::max()));
				if (config.DeterminismTicks == 0)
					throw std::invalid_argument("--determinism-check expects at least one tick");
			}
			// Anything else is the game's: `--orbit` becomes the option "orbit".
			else if (option.starts_with("--") && option.size() > 2)
				config.GameOptions.emplace_back(option.substr(2));
			else
				throw std::invalid_argument(std::string(Usage));
		}

		// Development reads the game's source content and rebuilds its module on save; --no-watch
		// and players run the staged content and the built module as they are.
		const bool develop = GameBuild::Enabled && !noWatch;
		if (playerBuild)
			config.ContentRoot = executableDirectory / "Content";
		else if (develop)
			config.ContentRoot = std::filesystem::path(GameBuild::Root) / "Games" / game / "Content";
		else
			config.ContentRoot = executableDirectory / "Games" / game / "Content";
		if (!std::filesystem::is_regular_file(config.ContentRoot / "Project.ravenproject"))
			throw std::invalid_argument("Unknown game '" + game + "': no Project.ravenproject in " + config.ContentRoot.string());
		// Path concatenation reads the UTF-8 suffix on every OS.
		if (GameBuild::Enabled)
			config.GameModulePath = executableDirectory / (std::filesystem::path(game) += GameBuild::ModuleSuffix);
		config.AutoBuildGame = develop;
		config.WatchGameModule = !noWatch;
		config.WatchContent = develop;
		config.WatchShaders = develop;
		config.DeveloperInput = !playerBuild;
		// Players write to the OS per-user folder; development keeps its data in the ignored build tree,
		// so test runs never touch a player's profile (docs/GameServicesDesign.md, FD-33).
		if (playerBuild)
			config.UserDataRoot = Paths::GetUserDataDirectory() / "RavenEngine" / game;
		else if (GameBuild::Enabled)
			config.UserDataRoot = std::filesystem::path(GameBuild::Root) / "out" / "User" / game;
		else
			config.UserDataRoot = executableDirectory / "User" / game;
		config.UserDataRoot = config.UserDataRoot.lexically_normal();
		// The determinism check runs the staged game unchanged, with a fixed seed and no user data, so
		// nothing outside the scene and its input differs between its two runs.
		if (config.DeterminismTicks != 0)
		{
			config.AutoBuildGame = config.WatchGameModule = config.WatchContent = config.WatchShaders = false;
			config.UserDataRoot.clear();
			if (!config.SimulationSeed)
				config.SimulationSeed = 1;
		}
		config.Renderer = presentation.Renderer;
		config.MaxFrameRate = presentation.MaxFrameRate;

		WindowDesc description;
		description.Title = playerBuild ? "RavenEngine" : "RavenEngine | " + game + (develop ? " | F5 reload" : "");
		if (presentation.BorderlessSize)
		{
			description.Position = presentation.Position;
			description.Borderless = true;
			description.Width = presentation.BorderlessSize->first;
			description.Height = presentation.BorderlessSize->second;
		}
		if (const char* trace = std::getenv("RAVEN_FRAME_TRACE"); trace && *trace)
			config.FrameTracePath = trace;
		Log(LogLevel::Info, "Game " + game + (develop ? ": edit Games/" + game + "/Source, the module rebuilds on save" :
			(noWatch ? ": watching disabled, using staged content" : "")));
		Log(LogLevel::Info, "Engine keys: F3 restart scene, F4 next scene, F5 reload game module, F7 render view, F8 pause, "
			"Shift+F8 restart play, F9 step while paused, F10 colliders, Ctrl+F5 quick-save, Ctrl+F9 quick-load");
		Application app;
		app.Run(description, config);
		return 0;
	}
	catch (const std::exception& error)
	{
		Log(LogLevel::Error, error.what());
		return 1;
	}
	catch (...)
	{
		Log(LogLevel::Error, "The engine stopped on an exception that is not a std::exception");
		return 1;
	}
}

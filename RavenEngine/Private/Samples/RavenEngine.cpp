// RavenEngine.cpp : Defines the entry point for the application.
//

#include "RavenEngine.h"
#include "Raven/Core/Application.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Window.hpp"
#include "PerspectiveSample.hpp"
#include "OrthographicSample.hpp"
#include <exception>
#include <stdexcept>
#include <string_view>
#include "Raven/Core/Log.hpp"

int main(int argc, char* argv[])
{
	try
	{
		bool orthographic = false;
		bool game = false;
		bool watchGame = false;
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
			if (option == "--orthographic")
				orthographic = true;
			else if (option == "--perspective")
				orthographic = false;
			else if (option == "--game")
				game = true;
			else if (option == "--watch-game")
				game = watchGame = true;
			else
				throw std::invalid_argument("Usage: RavenEngine [--perspective | --orthographic | --game | --watch-game]");
		}
		if (game) orthographic = true;
		Raven::WindowDesc description;
		description.Title = orthographic ? "RavenEngine | Orthographic 2D" :
			"RavenEngine | Transparency | Arrows orbit, wheel zoom, Home/R reset, Space pause, F1/O order";
		const Raven::ApplicationClientFactory factory
		{
			nullptr,
			[](Raven::ApplicationClient* client) noexcept
			{
				delete client;
			},
			[](Raven::VulkanContext& renderer, Raven::GameInstance& instance) -> Raven::ApplicationClient*
			{
				return new Raven::PerspectiveSample(renderer, instance);
			}
		};
		const Raven::ApplicationClientFactory orthographicFactory
		{
			nullptr, factory.Destroy,
			[](Raven::VulkanContext& renderer, Raven::GameInstance& instance) -> Raven::ApplicationClient*
			{
				return new Raven::OrthographicSample(renderer, instance);
			}
		};
		Raven::ApplicationConfig config;
		if (game)
		{
			config.GameModulePath = Raven::Paths::GetExecutableDirectory() / "RavenGame.dll";
			config.AutoBuildGame = watchGame;
			description.Title = "RavenEngine | ECS + Game reload | F5 reload";
			Raven::Log(Raven::LogLevel::Info, watchGame ?
				"Game source watching enabled; edit Private/Game/RavenGame.cpp. F5 reloads the built DLL." :
				"Game reload sample: build the standalone GameBuild project while running; F5 reloads the DLL.");
		}
		Raven::Log(Raven::LogLevel::Info,
			orthographic ? "Orthographic sample: arrows pan, wheel/PageUp/PageDown zoom, Home reset, F1 transparent order" :
			"Perspective sample: arrows orbit, wheel zoom, Home/R reset, Space pause, F1/O transparent order");
		Raven::Application app;
		app.Run(description, orthographic ? orthographicFactory : factory, config);

		return 0;
	}
	catch (const std::exception& error)
	{
		Raven::Log(Raven::LogLevel::Error, error.what());
		return 1;
	}
}

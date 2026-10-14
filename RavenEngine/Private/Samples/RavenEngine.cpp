// RavenEngine.cpp : Defines the entry point for the application.
//

#include "RavenEngine.h"
#include "Raven/Core/Application.hpp"
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
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
			if (option == "--orthographic")
				orthographic = true;
			else if (option == "--perspective")
				orthographic = false;
			else
				throw std::invalid_argument("Usage: RavenEngine [--perspective | --orthographic]");
		}
		Raven::WindowDesc description;
		description.Title = orthographic ? "RavenEngine | Orthographic 2D" :
			"RavenEngine | Transparency | Arrows orbit, wheel zoom, Home/R reset, Space pause, F1/O order";
		const Raven::ApplicationClientFactory factory
		{
			[](Raven::VulkanContext& renderer) -> Raven::ApplicationClient*
			{
				return new Raven::PerspectiveSample(renderer);
			},
			[](Raven::ApplicationClient* client) noexcept
			{
				delete client;
			}
		};
		const Raven::ApplicationClientFactory orthographicFactory
		{
			[](Raven::VulkanContext& renderer) -> Raven::ApplicationClient*
			{
				return new Raven::OrthographicSample(renderer);
			},
			factory.Destroy
		};
		Raven::Log(Raven::LogLevel::Info,
			orthographic ? "Orthographic sample: arrows pan, wheel/PageUp/PageDown zoom, Home reset, F1 transparent order" :
			"Perspective sample: arrows orbit, wheel zoom, Home/R reset, Space pause, F1/O transparent order");
		Raven::Application app;
		app.Run(description, orthographic ? orthographicFactory : factory);

		return 0;
	}
	catch (const std::exception& error)
	{
		Raven::Log(Raven::LogLevel::Error, error.what());
		return 1;
	}
}

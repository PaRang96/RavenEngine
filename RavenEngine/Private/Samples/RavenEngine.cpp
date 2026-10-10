// RavenEngine.cpp : Defines the entry point for the application.
//

#include "RavenEngine.h"
#include "Raven/Core/Application.hpp"
#include "Raven/Platform/Window.hpp"
#include "PerspectiveSample.hpp"
#include <exception>
#include "Raven/Core/Log.hpp"

int main()
{
	try
	{
		Raven::WindowDesc description;
		description.Title = "RavenEngine | Perspective | Arrows orbit, wheel zoom, Home/R reset, Space pause";
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
		Raven::Log(Raven::LogLevel::Info,
			"Perspective sample: arrows orbit, wheel zoom, Home/R reset, Space pause");
		Raven::Application app;
		app.Run(description, factory);

		return 0;
	}
	catch (const std::exception& error)
	{
		Raven::Log(Raven::LogLevel::Error, error.what());
		return 1;
	}
}

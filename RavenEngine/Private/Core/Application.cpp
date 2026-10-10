#include "Raven/Core/Application.hpp"
#include "Core/FrameClock.hpp"
#include "Core/Paths.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

#include "Raven/Platform/Window.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

namespace Raven
{
	void Application::Run(const WindowDesc& description, const ApplicationClientFactory& factory)
	{
		Run(description, factory, ApplicationConfig{});
	}

	void Application::Run(const WindowDesc& description, const ApplicationClientFactory& factory,
		const ApplicationConfig& config)
	{
		if (!factory.Create || !factory.Destroy)
			throw std::invalid_argument("Application requires client create and destroy callbacks");

		ApplicationPathsScope paths(config.ContentRoot);
		auto window = Window::Create(description);
		VulkanContext renderer(*window);
		// The client is destroyed through its own module before the renderer and window.
		std::unique_ptr<ApplicationClient, decltype(factory.Destroy)>
			client(factory.Create(renderer), factory.Destroy);
		if (!client)
			throw std::runtime_error("Application client factory returned null");
		FrameClock clock;

		while (!window->ShouldClose())
		{
			window->PollEvents();
			if (window->ShouldClose())
				break;

			if (window->GetFramebufferState().IsDrawable())
			{
				const FrameTime frameTime = clock.Tick();

				client->OnFrame(renderer, window->GetInputState(), frameTime);
			}
			else
			{
				clock.ResetDelta();
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
	}
}

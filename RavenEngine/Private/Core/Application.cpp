#include "Raven/Core/Application.hpp"
#include "Core/FrameClock.hpp"
#include "Core/Paths.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/GameModule.hpp"

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
		if ((!factory.Create && !factory.CreateGameClient) || !factory.Destroy)
			throw std::invalid_argument("Application requires client create and destroy callbacks");

		ApplicationPathsScope paths(config.ContentRoot);
		auto window = Window::Create(description);
		VulkanContext renderer(*window);
		GameInstance instance;
		std::unique_ptr<GameModule> game;
		if (!config.GameModulePath.empty())
			game = std::make_unique<GameModule>(config.GameModulePath, config.AutoBuildGame);
		// The retained host client dies before module, session, renderer and window.
		std::unique_ptr<ApplicationClient, decltype(factory.Destroy)>
			client(factory.CreateGameClient ? factory.CreateGameClient(renderer, instance) : factory.Create(renderer), factory.Destroy);
		if (!client)
			throw std::runtime_error("Application client factory returned null");
		FrameClock clock;

		while (!window->ShouldClose())
		{
			window->PollEvents();
			if (window->ShouldClose())
				break;
			if (game)
			{
				game->Poll();
				if (window->GetInputState().WasKeyPressed(Key::F5)) game->Reload();
			}

			if (window->GetFramebufferState().IsDrawable())
			{
				const FrameTime frameTime = clock.Tick();
				if (game) game->Update(instance, window->GetInputState(), frameTime);

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

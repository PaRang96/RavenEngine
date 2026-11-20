#pragma once

#include "Raven/Assets/AssetStore.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Platform/Window.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <memory>

// Scene needs an AssetStore, which needs a renderer and a window, so tests and
// benchmarks that use Scene are GPU-dependent. Create scenes after this object
// so they are destroyed before the registry and store.
namespace RavenTests
{
	struct TestWorld
	{
		explicit TestWorld(const char* title)
			: Window(Raven::Window::Create({ title, 320, 240 })),
			Renderer(*Window),
			Store(Renderer, Raven::Paths::GetContentPath())
		{
		}

		std::unique_ptr<Raven::Window> Window;
		Raven::VulkanContext Renderer;
		Raven::AssetStore Store;
		Raven::ECS::Registry Registry;
	};
}

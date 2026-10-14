#pragma once

#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"

#include <cstdint>

namespace Raven
{
	// Straight-alpha textures. Higher layer/order values are composited later.
	struct VulkanTransparentDraw
	{
		VulkanTexturedDraw Draw;
		std::int32_t SortLayer = 0;
		std::int32_t SortOrder = 0;
	};
}

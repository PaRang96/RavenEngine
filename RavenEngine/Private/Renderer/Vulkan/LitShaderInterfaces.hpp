#pragma once

#include "Renderer/Vulkan/ShaderInterface.hpp"

#include <cstdint>
#include <vector>

namespace Raven
{
	// The fixed interfaces of the lit pipeline (docs/HotReloadDesign.md, H2; RenderingContentDesign.md,
	// B0 contract 1): the renderer checks reloaded engine shaders against them, and the asset store
	// checks material shaders when they load.
	std::vector<std::uint32_t> FrameOffsets();
	std::vector<ShaderStageVariable> LitStageVariables();
	ShaderInterface LitVertexInterface();
	ShaderInterface LitFragmentInterface();
}

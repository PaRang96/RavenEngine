#pragma once

#include <cstdint>

namespace Raven
{
	// How lit meshes are drawn (docs/HotReloadDesign.md, H3). Debug views draw the same
	// GPU-culled commands with another shader; textured and transparent draws are unchanged.
	enum class RenderView : std::uint8_t
	{
		// Materials and lighting.
		Shaded,
		// Base color only, without lighting.
		Unlit,
		// World-space normals mapped to color.
		Normals,
		// Shaded, with triangle edges drawn over it. Needs the fillModeNonSolid device feature.
		Wireframe,
	};
}

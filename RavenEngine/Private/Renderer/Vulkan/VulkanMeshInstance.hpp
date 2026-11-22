#pragma once

#include "Raven/Math/Transform.hpp"

#include <array>
#include <cstddef>
#include <type_traits>

namespace Raven
{
	// Per-instance input of the lit vertex shader (docs/RendererDesign.md, R4). Camera and
	// lighting come from the frame uniform; the shader computes clip = ViewProjection x world.
	struct MeshInstanceData
	{
		// Rows of the 3x4 local-to-world matrix.
		std::array<Vec4, 3> WorldRows;
		// Columns of the normal matrix. NormalToWorld[0].W is 1 for lit meshes, 0 otherwise.
		std::array<Vec4, 3> NormalToWorld;
		Vec4 Tint;
	};

	static_assert(std::is_standard_layout_v<MeshInstanceData>);
	static_assert(std::is_trivially_copyable_v<MeshInstanceData>);
	static_assert(sizeof(MeshInstanceData) == 112);
	static_assert(offsetof(MeshInstanceData, NormalToWorld) == 48);
	static_assert(offsetof(MeshInstanceData, Tint) == 96);
}

#pragma once

#include "Raven/Math/Transform.hpp"

#include <array>
#include <cstddef>
#include <type_traits>

namespace Raven
{
	// Per-frame vertex input shared by the mesh vertex and fragment shaders.
	struct MeshInstanceData
	{
		Mat4 ClipFromLocal;
		std::array<Vec4, 3> NormalToWorld;
		Vec4 Tint;
		Vec4 AmbientAndLit;
		Vec4 LightDirection;
		Vec4 LightColor;
	};

	static_assert(std::is_standard_layout_v<MeshInstanceData>);
	static_assert(std::is_trivially_copyable_v<MeshInstanceData>);
	static_assert(sizeof(MeshInstanceData) == 176);
	static_assert(offsetof(MeshInstanceData, NormalToWorld) == 64);
	static_assert(offsetof(MeshInstanceData, Tint) == 112);
	static_assert(offsetof(MeshInstanceData, AmbientAndLit) == 128);
	static_assert(offsetof(MeshInstanceData, LightDirection) == 144);
	static_assert(offsetof(MeshInstanceData, LightColor) == 160);
}

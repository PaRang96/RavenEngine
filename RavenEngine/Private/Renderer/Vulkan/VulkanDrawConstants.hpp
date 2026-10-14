#pragma once

#include "Raven/Math/Transform.hpp"

#include <cstddef>
#include <type_traits>

namespace Raven
{
	// The color pipeline uses this 80-byte CPU/HLSL push-constant layout.
	struct DrawPushConstants
	{
		Mat4 ClipFromLocal;
		Vec4 Tint;
	};

	static_assert(std::is_standard_layout_v<DrawPushConstants>);
	static_assert(std::is_trivially_copyable_v<DrawPushConstants>);
	static_assert(sizeof(DrawPushConstants) == 80);
	static_assert(offsetof(DrawPushConstants, ClipFromLocal) == 0);
	static_assert(offsetof(DrawPushConstants, Tint) == 64);
}

#pragma once

#include <cstddef>
#include <type_traits>

namespace Raven
{
	struct ColorVertex
	{
		float Position[3];
		float Color[3];
	};

	static_assert(std::is_standard_layout_v<ColorVertex>);
	static_assert(std::is_trivially_copyable_v<ColorVertex>);
	static_assert(sizeof(ColorVertex) == 24);
	static_assert(offsetof(ColorVertex, Position) == 0);
	static_assert(offsetof(ColorVertex, Color) == 12);
}

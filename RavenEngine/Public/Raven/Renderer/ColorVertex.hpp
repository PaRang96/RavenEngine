#pragma once

#include <cstddef>
#include <type_traits>

namespace Raven
{
	struct ColorVertex
	{
		float Position[3];
		float Color[3];
		float Normal[3]{ 0.0f, 0.0f, 1.0f };
		float UV[2]{};
	};

	static_assert(std::is_standard_layout_v<ColorVertex>);
	static_assert(std::is_trivially_copyable_v<ColorVertex>);
	static_assert(sizeof(ColorVertex) == 44);
	static_assert(offsetof(ColorVertex, Position) == 0);
	static_assert(offsetof(ColorVertex, Color) == 12);
	static_assert(offsetof(ColorVertex, Normal) == 24);
	static_assert(offsetof(ColorVertex, UV) == 36);
}

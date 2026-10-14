#pragma once

#include <cstddef>
#include <type_traits>

namespace Raven
{
	struct TexturedVertex
	{
		float Position[3];
		float UV[2];
	};

	static_assert(std::is_standard_layout_v<TexturedVertex>);
	static_assert(std::is_trivially_copyable_v<TexturedVertex>);
	static_assert(sizeof(TexturedVertex) == 20);
	static_assert(offsetof(TexturedVertex, Position) == 0);
	static_assert(offsetof(TexturedVertex, UV) == 12);
}

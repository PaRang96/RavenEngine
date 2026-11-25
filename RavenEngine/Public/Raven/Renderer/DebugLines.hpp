#pragma once

#include "Raven/Math/Transform.hpp"

#include <cstdint>
#include <type_traits>

namespace Raven
{
	// One end of a developer debug line (docs/GameServicesDesign.md, FD-32 collider view). Lines are
	// vertex pairs in world space, depth-tested against the scene and drawn after it.
	struct DebugLineVertex
	{
		Vec3 Position;
		// 0xAABBGGRR, linear color.
		std::uint32_t Color = 0xffffffffu;
	};

	static_assert(std::is_trivially_copyable_v<DebugLineVertex> && sizeof(DebugLineVertex) == 16);

	constexpr std::uint32_t PackDebugColor(float red, float green, float blue, float alpha = 1.0f) noexcept
	{
		const auto channel = [](float value) -> std::uint32_t
		{
			const float clamped = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
			return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
		};
		return channel(red) | (channel(green) << 8) | (channel(blue) << 16) | (channel(alpha) << 24);
	}
}

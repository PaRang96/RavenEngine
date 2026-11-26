#pragma once

#include "Raven/Math/Transform.hpp"

#include <cmath>

namespace Raven
{
	// Draw gathering skips values that cannot be drawn instead of throwing (docs/DataModelDesign.md,
	// FD-06): the world pass keeps non-finite worlds by design, and consumers skip them.

	// A finite affine model matrix.
	inline bool IsDrawable(const Mat4& model) noexcept
	{
		for (const auto& column : model.Columns)
			for (const float value : column)
				if (!std::isfinite(value))
					return false;
		return model(3, 0) == 0.0f && model(3, 1) == 0.0f && model(3, 2) == 0.0f && model(3, 3) == 1.0f;
	}

	inline bool IsFinite(Vec4 value) noexcept
	{
		return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z) && std::isfinite(value.W);
	}
}

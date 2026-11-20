#pragma once

#include "Raven/Math/Transform.hpp"

#include <cmath>
#include <stdexcept>

namespace Raven
{
	struct SceneLighting
	{
		Vec3 Ambient{ 1.0f, 1.0f, 1.0f };
		// Direction in which the light travels, in world space.
		Vec3 Direction{ 0.0f, 0.0f, -1.0f };
		Vec3 Color{};

		void Validate() const
		{
			for (const float value : { Ambient.X, Ambient.Y, Ambient.Z, Color.X, Color.Y, Color.Z })
				if (!std::isfinite(value) || value < 0.0f)
					throw std::invalid_argument("Light colors must be finite and nonnegative");
			if (!std::isfinite(Ambient.X + Color.X) || !std::isfinite(Ambient.Y + Color.Y) ||
				!std::isfinite(Ambient.Z + Color.Z))
				throw std::invalid_argument("Combined lighting exceeds the supported numeric range");
			Normalize(Direction);
		}
	};
}

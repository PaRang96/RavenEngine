#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Math/Transform.hpp"

#include <span>

namespace Raven
{
	struct XzHalfExtents
	{
		float X = 0.0f;
		float Z = 0.0f;
	};

	struct XzBounds
	{
		Vec3 Center;
		XzHalfExtents HalfExtents;
	};

	// Touching counts for triggers and is permitted between solids.
	RAVEN_API bool OverlapsXZ(const XzBounds& first, const XzBounds& second) noexcept;
	RAVEN_API bool PenetratesXZ(const XzBounds& first, const XzBounds& second) noexcept;

	// Returns the resolved center, keeping Y. Bounds must have positive finite
	// extents, and the moving bounds must start outside the solid interiors.
	RAVEN_API Vec3 ResolveMovementXZ(const XzBounds& moving, Vec3 displacement,
		std::span<const XzBounds> solids) noexcept;
}

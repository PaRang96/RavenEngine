#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

namespace Raven::ECS
{
	struct TransformComponent : Component
	{
		Vec3 Position{};
		// Radians; local X, then Y, then Z rotation with column vectors.
		Vec3 Rotation{};
		Vec3 Scale{ 1.0f, 1.0f, 1.0f };

		RAVEN_COMPONENT(Raven::ECS::TransformComponent, Position, Rotation, Scale);

		Mat4 GetMatrix() const
		{
			return Translation(Position) * RotationZ(Rotation.Z) * RotationY(Rotation.Y) *
				RotationX(Rotation.X) * Raven::Scale(Scale);
		}
	};
}

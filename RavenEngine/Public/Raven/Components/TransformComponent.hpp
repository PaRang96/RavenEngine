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

	// Host-managed world matrix (docs/ECSDesign.md, D7). Scene::UpdateWorldTransforms adds and
	// refreshes it for scene entities with a TransformComponent; game code reads it only.
	// Gameplay needing a value composed after its own edits uses GetWorldTransform instead.
	struct WorldTransformComponent : Component
	{
		Mat4 World = Mat4::Identity();

		RAVEN_COMPONENT(Raven::ECS::WorldTransformComponent, World);
	};
}

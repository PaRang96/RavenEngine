#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"
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

	// The saved hierarchy (docs/DataModelDesign.md, FD-07): the parent of an entity that has one.
	// Scene::SetParent writes it and keeps its runtime child index in step; roots have none.
	struct ParentComponent : Component
	{
		EntityRef Parent;

		RAVEN_COMPONENT(Raven::ECS::ParentComponent, Parent);
	};

	// Opt-in render interpolation (docs/GameServicesDesign.md, FD-34). An entity moved in fixed steps
	// and tagged with this is drawn between its last two fixed ticks, so motion stays smooth when
	// frames outnumber steps. Game code sees the latest tick; picking matches what is drawn. Physics bodies and
	// characters get it automatically.
	struct InterpolateComponent : Component
	{
		RAVEN_COMPONENT(Raven::ECS::InterpolateComponent);
	};

	// Host tag (review amendment, AA2-GP-03): the entity was created in Control or Update, so its ID
	// came from the presentation sequence and it is outside the simulation. Cosmetic spawns (damage
	// numbers, debris) belong here; the determinism check leaves such entities out, and the physics
	// world warns once if one carries physics components. Not saved.
	struct PresentationComponent : Component
	{
		RAVEN_COMPONENT(Raven::ECS::PresentationComponent);
	};

	template<> inline constexpr bool HostOwned<WorldTransformComponent> = true;
	template<> inline constexpr bool Transient<WorldTransformComponent> = true;
	template<> inline constexpr bool HostOwned<ParentComponent> = true;
	template<> inline constexpr bool HostOwned<PresentationComponent> = true;
	template<> inline constexpr bool Transient<PresentationComponent> = true;
}

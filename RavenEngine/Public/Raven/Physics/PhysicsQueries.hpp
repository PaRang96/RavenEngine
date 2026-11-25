#pragma once

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace Raven
{
	enum class ContactEventKind : std::uint8_t
	{
		Begin,
		End,
	};

	// Two colliders started or stopped touching (docs/GameServicesDesign.md, FD-32). A is the
	// entity with the smaller persistent ID. Sensor, Point and Normal (from A toward B) are set on
	// Begin. Bodies falling asleep stay touching; a removed entity's contacts end at the next step.
	struct ContactEvent
	{
		ContactEventKind Kind = ContactEventKind::Begin;
		ECS::EntityRef A;
		ECS::EntityRef B;
		// True when either collider is a sensor.
		bool Sensor = false;
		Vec3 Point{};
		Vec3 Normal{};
	};

	struct PhysicsHit
	{
		ECS::Entity Entity;
		Vec3 Point{};
		// The surface normal at Point, facing the query.
		Vec3 Normal{};
		// Along the ray or sweep, in meters.
		float Distance = 0.0f;
	};

	// A query volume in world space; Size follows ColliderComponent.
	struct PhysicsShape
	{
		ECS::ColliderShape Shape = ECS::ColliderShape::Sphere;
		Vec3 Size{ 0.5f, 0.5f, 0.5f };
		Vec3 Position{};
		// XYZ Euler radians, like TransformComponent.
		Vec3 Rotation{};
	};

	// Collision queries and contact events of the scene's physics world, reached through
	// SystemContext::Physics. Queries see the world after the latest fixed step; changes game code
	// made since then reach it at the next step. Sensors are not hit by queries. Results are
	// borrowed for the current system call.
	class PhysicsQueries
	{
	public:
		// The closest hit along a ray. Direction need not be normalized.
		virtual std::optional<PhysicsHit> CastRay(Vec3 origin, Vec3 direction, float maxDistance,
			std::uint32_t layers = ECS::AllPhysicsLayers) const = 0;
		// The first hit of a shape moved along direction by up to maxDistance.
		virtual std::optional<PhysicsHit> CastShape(const PhysicsShape& shape, Vec3 direction, float maxDistance,
			std::uint32_t layers = ECS::AllPhysicsLayers) const = 0;
		// Entities whose colliders overlap the shape, ordered by persistent ID.
		virtual std::span<const ECS::Entity> Overlap(const PhysicsShape& shape,
			std::uint32_t layers = ECS::AllPhysicsLayers) const = 0;
		// During a fixed step: the contacts that began or ended in the previous physics step, ordered
		// by entity pair. Empty in the Control and Update phases.
		virtual std::span<const ContactEvent> GetContacts() const noexcept = 0;

	protected:
		~PhysicsQueries() = default;
	};
}

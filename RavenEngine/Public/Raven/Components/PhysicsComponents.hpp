#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstdint>

namespace Raven::ECS
{
	// Collision layers (docs/GameServicesDesign.md, FD-32). Every layer collides with every other
	// layer; queries choose layers with a mask.
	inline constexpr std::uint8_t PhysicsLayerCount = 16;
	inline constexpr std::uint32_t AllPhysicsLayers = (1u << PhysicsLayerCount) - 1;

	enum class ColliderShape : std::uint8_t
	{
		// Size is the half extent on each local axis.
		Box,
		// Size.X is the radius.
		Sphere,
		// Along local Z: Size.X is the radius, Size.Z half the length of the straight part.
		Capsule,
	};

	// A collision shape on the entity (FD-32). Without a RigidBodyComponent it is static. Sizes are
	// multiplied by the entity's world scale; spheres need a uniform scale and capsules an equal X
	// and Y scale. An invalid collider is logged once and the entity is left out of the world.
	struct ColliderComponent : Component
	{
		ColliderShape Shape = ColliderShape::Box;
		Vec3 Size{ 0.5f, 0.5f, 0.5f };
		// The shape's center in the entity's local space, before scale.
		Vec3 Offset{};
		// Reports contacts but does not push anything.
		bool Sensor = false;
		// Below PhysicsLayerCount.
		std::uint8_t Layer = 0;

		RAVEN_COMPONENT(Raven::ECS::ColliderComponent, Shape, Size, Offset, Sensor, Layer);
	};

	enum class MotionType : std::uint8_t
	{
		// Never moves by simulation; may follow its transform.
		Static,
		// Moved by game code through its transform; pushes dynamic bodies but nothing pushes it.
		Kinematic,
		// Moved by the simulation. Must be a scene root.
		Dynamic,
	};

	// Simulation values for an entity with a ColliderComponent (FD-32). The engine writes the
	// transform and both velocities of dynamic bodies after every fixed step; game code may write
	// them too, which teleports the body or changes its velocity. Velocities are component values,
	// so scene files, saves and field migration carry them.
	struct RigidBodyComponent : Component
	{
		MotionType Motion = MotionType::Dynamic;
		// Kilograms; dynamic bodies only.
		float Mass = 1.0f;
		float Friction = 0.5f;
		// 0 (no bounce) to 1.
		float Restitution = 0.0f;
		// World space, meters per second.
		Vec3 LinearVelocity{};
		// World space, radians per second.
		Vec3 AngularVelocity{};
		// Continuous collision for fast bodies such as projectiles, which could pass through thin walls.
		bool Continuous = false;

		RAVEN_COMPONENT(Raven::ECS::RigidBodyComponent, Motion, Mass, Friction, Restitution, LinearVelocity,
			AngularVelocity, Continuous);
	};

	enum class MoverBackend : std::uint8_t
	{
		// Precise, collision-correct movement for tens of characters (Jolt's virtual character).
		Character,
	};

	enum class MoverGround : std::uint8_t
	{
		// Standing on walkable ground.
		OnGround,
		// Touching ground too steep to stand on; it slides.
		OnSteepGround,
		// Touching something that does not hold it up.
		NotSupported,
		InAir,
	};

	// Moves an entity as a capsule that slides along walls, walks up slopes and steps, rides moving
	// ground and pushes dynamic bodies (docs/GameServicesDesign.md, FD-47). Game code writes the first
	// block in fixed steps; the engine writes the second after each step, together with the entity's
	// position. The entity must be a scene root without a ColliderComponent or RigidBodyComponent.
	// Writing its transform teleports it; writing Velocity changes its momentum.
	struct MoverComponent : Component
	{
		// World space, meters per second. With gravity, X and Y steer and a positive Z jumps when the
		// mover stands on walkable ground. Without gravity (GravityScale 0) all three steer.
		Vec3 DesiredVelocity{};
		// Limits the steering part of DesiredVelocity.
		float MaxSpeed = 6.0f;
		float GravityScale = 1.0f;
		// The capsule, centered on the entity: radius, and half the length of the straight part along Z.
		float Radius = 0.4f;
		float HalfHeight = 0.5f;
		// The steepest walkable slope, in radians.
		float MaxSlope = 0.87f;
		// Below PhysicsLayerCount; queries and sensors see the mover on this layer.
		std::uint8_t Layer = 0;
		MoverBackend Backend = MoverBackend::Character;

		Vec3 Velocity{};
		MoverGround Ground = MoverGround::InAir;
		Vec3 GroundNormal{};

		RAVEN_COMPONENT(Raven::ECS::MoverComponent, DesiredVelocity, MaxSpeed, GravityScale, Radius, HalfHeight,
			MaxSlope, Layer, Backend, Velocity, Ground, GroundNormal);
	};
}

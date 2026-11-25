#pragma once

// Components of the 3D sample game. The host never compiles them: the module declares them
// (RavenRegisterSystems), and scene files store them by name (docs/DataModelDesign.md, FD-17).

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Math/Transform.hpp"

#include <array>
#include <cstdint>

namespace Samples
{
	using Raven::Vec3;
	using Raven::ECS::EntityRef;

	struct SwitchComponent : Raven::ECS::Component
	{
		bool Active = false;
		RAVEN_COMPONENT(Samples::SwitchComponent, Active);
	};

	struct RotatorComponent : Raven::ECS::Component
	{
		// Null means continuous rotation at Rotation radians per second.
		EntityRef Switch;
		Vec3 BaseRotation{};
		// With a switch, the full offset reached when the switch is active.
		Vec3 Rotation{};
		float Duration = 1.0f;
		float Progress = 0.0f;
		Vec3 OffsetRotation{};
		RAVEN_COMPONENT(Samples::RotatorComponent, Switch, BaseRotation, Rotation, Duration, Progress, OffsetRotation);
	};

	struct DoorComponent : Raven::ECS::Component
	{
		std::array<EntityRef, 4> RequiredRotators{};
		std::uint32_t RequiredCount = 0;
		Vec3 ClosedPosition{};
		Vec3 OpenOffset{};
		float Duration = 1.0f;
		float Progress = 0.0f;
		RAVEN_COMPONENT(Samples::DoorComponent, RequiredRotators, RequiredCount, ClosedPosition, OpenOffset,
			Duration, Progress);
	};

	struct GoalComponent : Raven::ECS::Component
	{
		// Null means the goal is always open.
		EntityRef Door;
		float Radius = 1.0f;
		RAVEN_COMPONENT(Samples::GoalComponent, Door, Radius);
	};

	struct PickupComponent : Raven::ECS::Component
	{
		bool Collected = false;
		RAVEN_COMPONENT(Samples::PickupComponent, Collected);
	};

	// One per scene: the state of the current attempt.
	struct AttemptComponent : Raven::ECS::Component
	{
		bool Completed = false;
		bool Paused = false;
		bool Announced = false;
		bool CompletionNotified = false;
		std::uint32_t SwitchActivations = 0;
		RAVEN_COMPONENT(Samples::AttemptComponent, Completed, Paused, Announced, CompletionNotified, SwitchActivations);
	};

	// The orbit demo camera (`--orbit`): arrows orbit Target, the wheel zooms, Home or R returns
	// to the authored pose.
	struct OrbitCameraComponent : Raven::ECS::Component
	{
		Vec3 Target{};
		Vec3 HomePosition{};
		Vec3 HomeRotation{};
		RAVEN_COMPONENT(Samples::OrbitCameraComponent, Target, HomePosition, HomeRotation);
	};

	// The mesh T rotates in free flight, and the orbit demo animates.
	struct ControlledMeshComponent : Raven::ECS::Component
	{
		Vec3 InitialRotation{};
		RAVEN_COMPONENT(Samples::ControlledMeshComponent, InitialRotation);
	};

	// The goal marker; F6 swaps its mesh with the controlled mesh.
	struct BeaconComponent : Raven::ECS::Component
	{
		Vec3 InitialRotation{};
		RAVEN_COMPONENT(Samples::BeaconComponent, InitialRotation);
	};

	// One per scene: orbit-demo state and the sounds of the scene.
	struct DemoComponent : Raven::ECS::Component
	{
		Raven::AssetRef<Raven::AudioAsset> WinSound;
		// The orbit demo animates the controlled mesh and the beacon (scenes without authored motion).
		bool OrbitAnimation = false;
		bool Animate = true;
		float Angle = 0.0f;
		RAVEN_COMPONENT(Samples::DemoComponent, WinSound, OrbitAnimation, Animate, Angle);
	};

	// A mover that walks around a circle in the Physics scene (docs/GameServicesDesign.md, FD-47).
	struct PatrolComponent : Raven::ECS::Component
	{
		Raven::Vec3 Center{};
		float Radius = 3.0f;
		// Meters per second along the circle; negative walks the other way.
		float Speed = 3.0f;
		float Angle = 0.0f;
		RAVEN_COMPONENT(Samples::PatrolComponent, Center, Radius, Speed, Angle);
	};

	// The transparent sprite whose draw order O or F1 toggles.
	struct OrderToggleComponent : Raven::ECS::Component
	{
		RAVEN_COMPONENT(Samples::OrderToggleComponent);
	};
}

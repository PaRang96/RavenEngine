#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

#include <array>
#include <cstdint>

namespace Raven::ECS
{
	// Fixed UTF-8 authored references are copied values, never borrowed strings or runtime handles.
	using SceneReference = std::array<char, 129>;

	struct SwitchComponent : Component
	{
		bool Active = false;
		RAVEN_COMPONENT(Raven::ECS::SwitchComponent, Active);
	};

	struct RotatorComponent : Component
	{
		SceneReference Switch{};
		Vec3 BaseRotation{};
		// Empty Switch means continuous radians/second; otherwise this is the full activated offset.
		Vec3 Rotation{};
		float Duration = 1.0f;
		float Progress = 0.0f;
		Vec3 OffsetRotation{};
		RAVEN_COMPONENT(Raven::ECS::RotatorComponent, Switch, BaseRotation, Rotation, Duration, Progress,
						OffsetRotation);
	};

	struct DoorComponent : Component
	{
		std::array<SceneReference, 4> RequiredRotators{};
		std::uint32_t RequiredCount = 0;
		Vec3 ClosedPosition{};
		Vec3 OpenOffset{};
		float Duration = 1.0f;
		float Progress = 0.0f;
		RAVEN_COMPONENT(Raven::ECS::DoorComponent, RequiredRotators, RequiredCount, ClosedPosition,
						OpenOffset, Duration, Progress);
	};

	struct GoalComponent : Component
	{
		SceneReference Door{};
		float Radius = 1.0f;
		RAVEN_COMPONENT(Raven::ECS::GoalComponent, Door, Radius);
	};

	struct PickupComponent : Component
	{
		bool Collected = false;
		RAVEN_COMPONENT(Raven::ECS::PickupComponent, Collected);
	};

	struct AttemptComponent : Component
	{
		bool Completed = false;
		bool Paused = false;
		bool Announced = false;
		std::uint32_t SwitchActivations = 0;
		RAVEN_COMPONENT(Raven::ECS::AttemptComponent, Completed, Paused, Announced, SwitchActivations);
	};
}

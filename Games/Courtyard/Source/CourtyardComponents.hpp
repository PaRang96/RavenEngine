#pragma once

// Components of the 2D Courtyard game. The host never compiles them: the module declares them,
// and scene files store them by name (docs/DataModelDesign.md, FD-17).

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstdint>

namespace Courtyard
{
	using Raven::Vec3;

	// Fixed-step motion that bounces between X = -3 and X = 3.
	struct VelocityComponent : Raven::ECS::Component
	{
		Vec3 Linear{};
		RAVEN_COMPONENT(Courtyard::VelocityComponent, Linear);
	};

	// An XZ box around the entity's position. Solid boxes stop the player; others only overlap.
	struct ColliderComponent : Raven::ECS::Component
	{
		float HalfX = 0.5f;
		float HalfZ = 0.5f;
		bool Solid = false;
		RAVEN_COMPONENT(Courtyard::ColliderComponent, HalfX, HalfZ, Solid);
	};

	// WASD moves it; touching a hazard loses, touching the goal wins.
	struct PlayerComponent : Raven::ECS::Component
	{
		RAVEN_COMPONENT(Courtyard::PlayerComponent);
	};

	struct HazardComponent : Raven::ECS::Component
	{
		RAVEN_COMPONENT(Courtyard::HazardComponent);
	};

	struct GoalComponent : Raven::ECS::Component
	{
		RAVEN_COMPONENT(Courtyard::GoalComponent);
	};

	// The transparent sprite whose draw order F1 toggles.
	struct OrderToggleComponent : Raven::ECS::Component
	{
		RAVEN_COMPONENT(Courtyard::OrderToggleComponent);
	};

	// A camera F2 can switch to; Home returns the active one to this pose and height.
	struct CameraHomeComponent : Raven::ECS::Component
	{
		Vec3 Position{};
		Vec3 Rotation{};
		float OrthographicHeight = 6.0f;
		RAVEN_COMPONENT(Courtyard::CameraHomeComponent, Position, Rotation, OrthographicHeight);
	};

	enum class AttemptResult : std::uint8_t
	{
		Playing,
		Won,
		Lost
	};

	// One per scene: the attempt and its sounds.
	struct AttemptComponent : Raven::ECS::Component
	{
		AttemptResult Result = AttemptResult::Playing;
		Raven::AssetRef<Raven::AudioAsset> WinSound;
		Raven::AssetRef<Raven::AudioAsset> LossSound;
		RAVEN_COMPONENT(Courtyard::AttemptComponent, Result, WinSound, LossSound);
	};
}

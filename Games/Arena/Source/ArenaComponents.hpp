#pragma once

// Components and records of the arena game (docs/GameServicesDesign.md, FD-46). The host never
// compiles them: the module declares them (RavenRegisterSystems), and scene and user-data files
// store them by name (docs/DataModelDesign.md, FD-17).

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstdint>

namespace Arena
{
	using Raven::Vec3;
	using Raven::Vec4;

	// Collision layers: the world, the craft, drones and energy cells.
	inline constexpr std::uint8_t WorldLayer = 0;
	inline constexpr std::uint8_t CraftLayer = 1;
	inline constexpr std::uint8_t DroneLayer = 2;
	inline constexpr std::uint8_t CellLayer = 3;

	// The player's craft, a mover steered through the input stream (FD-47).
	struct CraftComponent : Raven::ECS::Component
	{
		float Speed = 7.0f;
		float JumpSpeed = 6.0f;
		// Where a run starts.
		Vec3 Spawn{};
		RAVEN_COMPONENT(Arena::CraftComponent, Speed, JumpSpeed, Spawn);
	};

	// The input-stream record a controller emits each frame: the steering direction on the floor,
	// already turned by the camera, and whether jump was pressed.
	struct CraftCommand : Raven::ECS::Component
	{
		Vec3 Move{};
		bool Jump = false;
		RAVEN_COMPONENT(Arena::CraftCommand, Move, Jump);
	};

	// A roaming drone: wanders between random waypoints and chases the craft within range.
	struct DroneComponent : Raven::ECS::Component
	{
		Vec3 Waypoint{};
		float Speed = 4.0f;
		float ChaseRange = 7.0f;
		// Seconds until the next waypoint.
		float Retarget = 0.0f;
		RAVEN_COMPONENT(Arena::DroneComponent, Waypoint, Speed, ChaseRange, Retarget);
	};

	// An energy cell: a sensor the craft collects.
	struct CellComponent : Raven::ECS::Component
	{
		std::uint32_t Value = 1;
		RAVEN_COMPONENT(Arena::CellComponent, Value);
	};

	// A kinematic platform sliding between two points.
	struct PlatformComponent : Raven::ECS::Component
	{
		Vec3 From{};
		Vec3 To{};
		// Seconds for a full trip there and back.
		float Period = 8.0f;
		float Time = 0.0f;
		RAVEN_COMPONENT(Arena::PlatformComponent, From, To, Period, Time);
	};

	// The authored rules of the arena, on the scene's `$arena` entity.
	struct ArenaRulesComponent : Raven::ECS::Component
	{
		// The floor spans [-HalfSize, HalfSize] in X and Y, inside the walls.
		float HalfSize = 14.0f;
		std::uint32_t Cells = 5;
		std::uint32_t StartDrones = 3;
		std::uint32_t MaxDrones = 14;
		// Seconds between new drones.
		float DroneInterval = 6.0f;
		// The `--swarm` launch option: drones for the character-backend measurement, and no game over.
		std::uint32_t SwarmDrones = 120;
		Raven::AssetRef<Raven::MeshAsset> CellMesh;
		Raven::AssetRef<Raven::MeshAsset> DroneMesh;
		// Sound-event assets (docs/GameServicesDesign.md, C8): designers tune the .ravensound files.
		Raven::AssetRef<Raven::SoundAsset> PickupSound;
		Raven::AssetRef<Raven::SoundAsset> JumpSound;
		Raven::AssetRef<Raven::SoundAsset> CaughtSound;
		Raven::AssetRef<Raven::SoundAsset> DroneSound;
		Raven::AssetRef<Raven::SoundAsset> ClickSound;
		Raven::AssetRef<Raven::SoundAsset> PauseSound;
		RAVEN_COMPONENT(Arena::ArenaRulesComponent, HalfSize, Cells, StartDrones, MaxDrones, DroneInterval, SwarmDrones,
			CellMesh, DroneMesh, PickupSound, JumpSound, CaughtSound, DroneSound, ClickSound, PauseSound);
	};

	enum class RunState : std::uint8_t
	{
		// The next fixed step clears the arena and starts a run.
		Starting,
		Playing,
		// A drone caught the craft; a new run starts when Timer runs out.
		Over,
	};

	// The run in progress: session state on the `$arena` entity (FD-34).
	struct ArenaRunComponent : Raven::ECS::Component
	{
		RunState State = RunState::Starting;
		std::uint32_t Score = 0;
		// The profile's best score when the run started, raised when the run beats it.
		std::uint32_t Best = 0;
		std::uint32_t Drones = 0;
		// Seconds since the run started.
		float Time = 0.0f;
		float NextDrone = 0.0f;
		float Timer = 0.0f;
		RAVEN_COMPONENT(Arena::ArenaRunComponent, State, Score, Best, Drones, Time, NextDrone, Timer);
	};

	// The follow camera's rig: local view state, never simulation state (FD-47).
	struct FollowCameraComponent : Raven::ECS::Component
	{
		// Behind and above the craft, before turning by Yaw.
		float Distance = 12.0f;
		float Height = 11.0f;
		float Pitch = -0.74f;
		float Yaw = 0.0f;
		// Radians per second for the turn keys.
		float TurnSpeed = 1.8f;
		// How quickly the camera catches up, per second.
		float Follow = 8.0f;
		RAVEN_COMPONENT(Arena::FollowCameraComponent, Distance, Height, Pitch, Yaw, TurnSpeed, Follow);
	};

	// The player's profile record in the user-data store (FD-33).
	struct ArenaProfile : Raven::ECS::Component
	{
		std::uint32_t BestScore = 0;
		std::uint32_t Runs = 0;
		RAVEN_COMPONENT(Arena::ArenaProfile, BestScore, Runs);
	};
}

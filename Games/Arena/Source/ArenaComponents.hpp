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

	// The craft's latched steering intent. A frame's records reach only its first fixed step, so the
	// craft steers from the latest record on every step, including catch-up steps below 60 FPS; a
	// jump stays pending until one step applies it. CraftSystem adds it to a craft that lacks it.
	struct CraftInputComponent : Raven::ECS::Component
	{
		Vec3 Move{};
		bool Jump = false;
		RAVEN_COMPONENT(Arena::CraftInputComponent, Move, Jump);
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
		// Below this height the craft has left the arena: the run ends, or in swarm mode the craft
		// returns to its spawn point. Far below the floor (z = 0) and the deepest jump landing.
		float FallOutZ = -5.0f;
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
			FallOutZ, CellMesh, DroneMesh, PickupSound, JumpSound, CaughtSound, DroneSound, ClickSound, PauseSound);
	};

	enum class RunState : std::uint8_t
	{
		// The next fixed step clears the arena and starts a run.
		Starting,
		Playing,
		// The run ended (RunEnd says how); a new run starts when Timer runs out.
		Over,
	};

	enum class RunEnd : std::uint8_t
	{
		Caught,
		FellOut,
	};

	// The run in progress: session state on the `$arena` entity (FD-34).
	struct ArenaRunComponent : Raven::ECS::Component
	{
		RunState State = RunState::Starting;
		std::uint32_t Score = 0;
		// The best score before this run. Fixed steps carry it from run to run; the Update-phase run
		// recorder raises it to the profile's best between runs, so fixed steps never read user data.
		std::uint32_t Best = 0;
		std::uint32_t Drones = 0;
		// Seconds since the run started.
		float Time = 0.0f;
		float NextDrone = 0.0f;
		float Timer = 0.0f;
		RunEnd End = RunEnd::Caught;
		// A finished run the run recorder has not written to the profile yet.
		bool Unrecorded = false;
		RAVEN_COMPONENT(Arena::ArenaRunComponent, State, Score, Best, Drones, Time, NextDrone, Timer, End, Unrecorded);
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

	enum class MenuScreen : std::uint8_t { Main, Bindings };

	// The pause menu's local view state on the camera rig entity; never simulation state (FD-47).
	struct ArenaMenuComponent : Raven::ECS::Component
	{
		MenuScreen Screen = MenuScreen::Main;
		// The binding row waiting for a key, or -1, and the binding within it.
		std::int32_t Listening = -1;
		// True on the frame a screen opens, so its first button takes keyboard focus.
		bool FocusPending = true;
		std::uint32_t Slot = 0;
		RAVEN_COMPONENT(Arena::ArenaMenuComponent, Screen, Listening, FocusPending, Slot);
	};

	// The player's profile record in the user-data store (FD-33).
	struct ArenaProfile : Raven::ECS::Component
	{
		std::uint32_t BestScore = 0;
		std::uint32_t Runs = 0;
		RAVEN_COMPONENT(Arena::ArenaProfile, BestScore, Runs);
	};
}

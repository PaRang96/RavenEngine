#pragma once

// The arena's systems (docs/GameServicesDesign.md, FD-46), one file per object or screen, and the
// helpers they share. ArenaGame.cpp registers them in phase order.
//
// Layers (FD-47): controllers in Control turn the mapped actions into CraftCommand records; fixed
// steps read them, run the drones and the rules, and write the movers; the camera rig, the run
// recorder and the screens in Update read the result.
//
// Optional and data-dependent reads go through a checked TryGet or Read (or FindCraft), never
// `TryGet(...)->`: a scene edited with dump/apply may lack any component, and a system that skips
// with one log line recovers as soon as the data is fixed (FD-06).

#include "ArenaComponents.hpp"

#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Core/UserData.hpp"
#include "Raven/Math/SimulationMath.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace Arena
{
	using namespace Raven;
	namespace Math = Raven::SimulationMath;

	inline constexpr std::string_view PlayContext = "Arena.Play";
	inline constexpr std::string_view PauseContext = "Arena.Pause";
	// Action names, shared by the registration, the systems and the key-binding screen.
	inline constexpr std::string_view MoveXAction = "MoveX";
	inline constexpr std::string_view MoveYAction = "MoveY";
	inline constexpr std::string_view JumpAction = "Jump";
	inline constexpr std::string_view TurnAction = "Turn";
	inline constexpr std::string_view PauseAction = "Pause";
	inline constexpr std::string_view ResumeAction = "Resume";
	inline constexpr ActionId MoveX{ MoveXAction };
	inline constexpr ActionId MoveY{ MoveYAction };
	inline constexpr ActionId Jump{ JumpAction };
	inline constexpr ActionId Turn{ TurnAction };
	inline constexpr ActionId Pause{ PauseAction };
	inline constexpr ActionId Resume{ ResumeAction };
	inline constexpr std::string_view ProfileFile = "Profile";

	inline float StepSeconds(const SystemContext& context)
	{
		return static_cast<float>(context.Time.DeltaSeconds);
	}

	// Frame phases: the first drawable frame after a pause carries a zero delta; long stalls are capped.
	inline float FrameSeconds(const SystemContext& context)
	{
		return static_cast<float>(std::clamp(context.Time.DeltaSeconds, 0.0, 0.1));
	}

	inline Vec3 Flat(Vec3 value)
	{
		return { value.X, value.Y, 0.0f };
	}

	inline float FlatLength(Vec3 value)
	{
		return Math::Sqrt(value.X * value.X + value.Y * value.Y);
	}

	// The camera's forward direction on the floor for a yaw (rotation about Z; zero looks along +Y).
	inline Vec3 Forward(float yaw)
	{
		return { -std::sin(yaw), std::cos(yaw), 0.0f };
	}

	template<typename T>
	ECS::Entity First(const SceneAccess& scene)
	{
		ECS::Entity found;
		scene.View<const T>().Each([&found](ECS::Entity entity, const T&)
		{
			found = entity;
			return false;
		});
		return found;
	}

	// Entities with T in persistent-ID order, so fixed steps that draw random numbers or spawn visit
	// them in the same order in every run (FD-48).
	template<typename T>
	std::vector<ECS::Entity> InIdOrder(const SceneAccess& scene)
	{
		std::vector<std::pair<std::uint64_t, ECS::Entity>> found;
		scene.View<const T>().Each([&](ECS::Entity entity, const T&) { found.emplace_back(scene.GetRef(entity).Id, entity); });
		std::sort(found.begin(), found.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
		std::vector<ECS::Entity> entities;
		entities.reserve(found.size());
		for (const auto& [id, entity] : found)
			entities.push_back(entity);
		return entities;
	}

	inline bool Swarm(const SystemContext& context)
	{
		return context.Services.HasOption("swarm");
	}

	// The one new-best rule, for the run recorder's profile write, its log and the HUD banner.
	inline bool IsNewBest(const ArenaRunComponent& run)
	{
		return run.Score > run.Best;
	}

	// The craft with the components its systems read.
	struct CraftState
	{
		ECS::Entity Entity;
		const CraftComponent* Craft = nullptr;
		const ECS::TransformComponent* Transform = nullptr;
		const ECS::MoverComponent* Mover = nullptr;
	};

	// ArenaSystems.cpp: the craft, or nothing when the scene has none. A craft that lacks its
	// Transform or Mover is reported once and skipped until the data is fixed.
	std::optional<CraftState> FindCraft(const SceneAccess& scene);
	// The profile store, or null in hosts without a user-data folder (tools and smoke tests), and
	// random floor points.
	UserDataStore* Profiles(SystemContext& context);
	Vec3 RandomPoint(SystemContext& context, const ArenaRulesComponent& rules, float margin, float z);
	// A random floor point clear of the world and at least `distance` from `avoid`; the last try
	// when none is found.
	Vec3 FreePoint(SystemContext& context, const ArenaRulesComponent& rules, Vec3 avoid, float distance, float z,
		float radius);
	ECS::MaterialComponent Paint(bool lit);

	// Spawning, used by the run flow and the cells.
	void SpawnDrone(SystemContext& context, const ArenaRulesComponent& rules, Vec3 craft);
	void SpawnCell(SystemContext& context, const ArenaRulesComponent& rules, Vec3 craft);

	// Run.cpp: the one way a run ends (caught or fallen out), for the fixed steps. The run recorder
	// writes the profile later, in Update.
	void EndRun(SystemContext& context, ArenaRunComponent& run, RunEnd end);

	// PauseMenu.cpp: the pause menu's view state on the camera rig, added when first needed (it
	// exists from the next frame); a screen as it opens; the interface sounds; and pausing.
	ArenaMenuComponent* Menu(SystemContext& context, bool add = false);
	ArenaMenuComponent OpenedScreen(MenuScreen screen);
	void PlayUiSound(SystemContext& context, bool pause);
	void SetPaused(SystemContext& context, bool paused);
	// Bindings.cpp: the key-binding screen while it is open.
	void BindingsScreen(SystemContext& context, ArenaMenuComponent& menu, bool focus);

	// Control: once per frame, before the fixed steps.
	void PauseSystem(SystemContext& context);
	void MenuSystem(SystemContext& context);
	void CraftControlSystem(SystemContext& context);
	// Fixed steps.
	void PlatformSystem(SystemContext& context);
	void RunSystem(SystemContext& context);
	void CraftSystem(SystemContext& context);
	void DroneSystem(SystemContext& context);
	void CellSystem(SystemContext& context);
	void HitSystem(SystemContext& context);
	// Update: once per frame, after the fixed steps.
	void CameraSystem(SystemContext& context);
	void RunRecordSystem(SystemContext& context);
	void HudSystem(SystemContext& context);
}

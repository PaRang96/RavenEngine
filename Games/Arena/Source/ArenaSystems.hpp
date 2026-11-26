#pragma once

// The arena's systems (docs/GameServicesDesign.md, FD-46), one file per object or screen, and the
// helpers they share. ArenaGame.cpp registers them in phase order.
//
// Layers (FD-47): controllers in Control turn the mapped actions into CraftCommand records; fixed
// steps read them, run the drones and the rules, and write the movers; the camera rig in Update
// follows the craft.

#include "ArenaComponents.hpp"

#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Core/UserData.hpp"
#include "Raven/Math/SimulationMath.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace Arena
{
	using namespace Raven;
	namespace Math = Raven::SimulationMath;

	inline constexpr std::string_view PlayContext = "Arena.Play";
	inline constexpr std::string_view PauseContext = "Arena.Pause";
	inline constexpr ActionId MoveX{ "MoveX" };
	inline constexpr ActionId MoveY{ "MoveY" };
	inline constexpr ActionId Jump{ "Jump" };
	inline constexpr ActionId Turn{ "Turn" };
	inline constexpr ActionId Pause{ "Pause" };
	inline constexpr ActionId Resume{ "Resume" };
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

	// ArenaSystems.cpp: the profile store, or null in hosts without a user-data folder (tools and
	// smoke tests), and random floor points.
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
	void HudSystem(SystemContext& context);
}

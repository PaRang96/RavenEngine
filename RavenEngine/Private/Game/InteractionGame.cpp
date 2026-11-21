#include "InteractionGame.hpp"
#include "Raven/Components/InteractionComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string_view>

namespace Raven::SampleGame
{
	namespace
	{
		std::string_view Name(const ECS::SceneReference& value)
		{
			return {value.data(),
					static_cast<std::size_t>(std::find(value.begin(), value.end(), '\0') - value.begin())};
		}
		float Approach(float progress, bool active, float delta, float duration)
		{
			constexpr float InteractionSpeed = 1.0f;
			return std::clamp(progress + (active ? 1.0f : -1.0f) * delta * InteractionSpeed / duration, 0.0f,
							  1.0f);
		}

		// Collects picked pickups and reports whether every scene pickup is collected.
		bool UpdatePickups(const SceneAccess& scene, std::span<const ECS::Entity> interactions)
		{
			bool changed = false;
			for (const auto entity : interactions)
			{
				auto* pickup = scene.TryGet<ECS::PickupComponent>(entity);
				if (!pickup || pickup->Collected)
					continue;
				pickup->Collected = true;
				if (auto* mesh = scene.TryGet<ECS::StaticMeshComponent>(entity))
					mesh->Tint = {.25f, .32f, .36f, 1};
				changed = true;
			}
			std::size_t total = 0;
			std::size_t collected = 0;
			scene.View<const ECS::PickupComponent>().Each([&](ECS::Entity, const ECS::PickupComponent& pickup)
			{
				++total;
				collected += pickup.Collected ? 1 : 0;
			});
			if (changed)
			{
				Log(LogLevel::Info, std::format("Collected {}/{}", collected, total));
				if (collected == total)
					Log(LogLevel::Info, "All pickups collected. Fly to the cyan exit.");
			}
			return collected == total;
		}
	}

	void UpdateInteractionGame(GameBindings bindings, const InputState& input, float delta)
	{
		const auto scene = bindings.World;
		if (!scene)
			return;
		auto* const attemptState = scene.Single<ECS::AttemptComponent>();
		if (!attemptState)
			return;
		auto& attempt = *attemptState;
		const bool hasPickups = scene.Count<ECS::PickupComponent>() != 0;
		if (!attempt.Announced)
		{
			if (hasPickups)
				Log(LogLevel::Info, "Collection: left-click the gold pickups, then fly to the cyan exit. F3 restarts.");
			else if (scene.Count<ECS::SwitchComponent>() != 0)
				Log(LogLevel::Info, "Puzzle: left-click both orange switches, wait for the gate to open, then fly to the cyan goal. F3 restarts.");
			attempt.Announced = true;
		}
		const bool readyToExit = attempt.Completed || !hasPickups || UpdatePickups(scene, bindings.Interactions);
		if (input.WasKeyPressed(Key::Space))
			attempt.Paused = !attempt.Paused;
		if (!attempt.Completed)
			for (const auto entity : bindings.Interactions)
			{
				auto* state = scene.TryGet<ECS::SwitchComponent>(entity);
				if (!state)
					continue;
				state->Active = !state->Active;
				++attempt.SwitchActivations;
				if (auto* mesh = scene.TryGet<ECS::StaticMeshComponent>(entity))
					mesh->Tint = state->Active ? Vec4{.1f, 1, .15f, 1} : Vec4{1, .4f, .1f, 1};
				Log(LogLevel::Info, state->Active ? "Switch activated" : "Switch deactivated");
			}
		if (!attempt.Paused)
		{
			scene.View<ECS::RotatorComponent, ECS::TransformComponent>().Each(
				[&](ECS::Entity, ECS::RotatorComponent& rotor, ECS::TransformComponent& transform)
				{
					if (Name(rotor.Switch).empty())
					{
						constexpr float ContinuousRotationSpeed = 1.0f;
						const auto offset = rotor.OffsetRotation + rotor.Rotation * (delta * ContinuousRotationSpeed);
						const float turn = 2.0f * std::numbers::pi_v<float>;
						rotor.OffsetRotation = {std::remainder(offset.X, turn), std::remainder(offset.Y, turn),
												std::remainder(offset.Z, turn)};
						transform.Rotation = rotor.BaseRotation + rotor.OffsetRotation;
					}
					else
					{
						const auto* state = scene.TryGet<const ECS::SwitchComponent>(scene.Find(Name(rotor.Switch)));
						rotor.Progress = Approach(rotor.Progress, state && state->Active, delta, rotor.Duration);
						transform.Rotation = rotor.BaseRotation + rotor.Rotation * rotor.Progress;
					}
				});
			scene.View<ECS::DoorComponent, ECS::TransformComponent>().Each(
				[&](ECS::Entity, ECS::DoorComponent& door, ECS::TransformComponent& transform)
				{
					bool ready = door.RequiredCount > 0 && door.RequiredCount <= door.RequiredRotators.size();
					for (std::size_t i = 0; ready && i < door.RequiredCount; ++i)
					{
						const auto* rotor = scene.TryGet<const ECS::RotatorComponent>(scene.Find(Name(door.RequiredRotators[i])));
						ready = rotor && rotor->Progress >= 1;
					}
					const auto before = door.Progress;
					door.Progress = Approach(door.Progress, ready, delta, door.Duration);
					transform.Position = door.ClosedPosition + door.OpenOffset * door.Progress;
					if (before < 1 && door.Progress >= 1)
						Log(LogLevel::Info, "Gate opened");
				});
		}
		if (!attempt.Completed && readyToExit && scene.IsAlive(bindings.Camera))
		{
			const auto camera = scene.GetWorldTransform(bindings.Camera);
			scene.View<const ECS::GoalComponent, const ECS::TransformComponent>().Each(
				[&](ECS::Entity entity, const ECS::GoalComponent& goal, const ECS::TransformComponent&)
				{
					if (!Name(goal.Door).empty())
					{
						const auto* door = scene.TryGet<const ECS::DoorComponent>(scene.Find(Name(goal.Door)));
						if (!door || door->Progress < 1)
							return true;
					}
					const auto world = scene.GetWorldTransform(entity);
					if (std::hypot(camera(0, 3) - world(0, 3), camera(1, 3) - world(1, 3),
								   camera(2, 3) - world(2, 3)) > goal.Radius)
						return true;
					attempt.Completed = true;
					if (auto* mesh = scene.TryGet<ECS::StaticMeshComponent>(entity))
						mesh->Tint = {.12f, 1, .12f, 1};
					return false;
				});
		}
	}
}

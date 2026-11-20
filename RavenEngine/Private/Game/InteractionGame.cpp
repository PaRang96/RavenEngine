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

		bool UpdatePickups(const SceneAccess& scene, std::span<const ECS::Entity> pickups,
						   std::span<const ECS::Entity> interactions)
		{
			bool changed = false;
			for (const auto entity : interactions)
			{
				ECS::PickupComponent pickup;
				if (!scene.Read(entity, pickup) || pickup.Collected)
					continue;
				pickup.Collected = true;
				scene.Write(entity, pickup);
				ECS::StaticMeshComponent mesh;
				if (scene.Read(entity, mesh))
				{
					mesh.Tint = {.25f, .32f, .36f, 1};
					scene.Write(entity, mesh);
				}
				changed = true;
			}
			const auto collected = static_cast<std::size_t>(std::count_if(pickups.begin(), pickups.end(),
				[&](ECS::Entity entity)
				{
					ECS::PickupComponent pickup;
					return scene.Read(entity, pickup) && pickup.Collected;
				}));
			if (changed)
			{
				Log(LogLevel::Info, std::format("Collected {}/{}", collected, pickups.size()));
				if (collected == pickups.size())
					Log(LogLevel::Info, "All pickups collected. Fly to the cyan exit.");
			}
			return collected == pickups.size();
		}
	}

	void UpdateInteractionGame(GameBindings bindings, const InputState& input, float delta)
	{
		const auto scene = bindings.World;
		if (!scene)
			return;
		const auto attempts = scene.Query<ECS::AttemptComponent>();
		if (attempts.empty())
			return;
		const auto attemptEntity = attempts.front();
		ECS::AttemptComponent attempt;
		if (!scene.Read(attemptEntity, attempt))
			return;
		const auto pickups = scene.Query<ECS::PickupComponent>();
		if (!attempt.Announced)
		{
			if (!pickups.empty())
				Log(LogLevel::Info, "Collection: left-click the gold pickups, then fly to the cyan exit. F3 restarts.");
			else if (!scene.Query<ECS::SwitchComponent>().empty())
				Log(LogLevel::Info, "Puzzle: left-click both orange switches, wait for the gate to open, then fly to the cyan goal. F3 restarts.");
			attempt.Announced = true;
		}
		const bool readyToExit =
			attempt.Completed || pickups.empty() || UpdatePickups(scene, pickups, bindings.Interactions);
		if (input.WasKeyPressed(Key::Space))
			attempt.Paused = !attempt.Paused;
		if (!attempt.Completed)
			for (const auto entity : bindings.Interactions)
			{
				ECS::SwitchComponent state;
				if (!scene.Read(entity, state))
					continue;
				state.Active = !state.Active;
				scene.Write(entity, state);
				++attempt.SwitchActivations;
				ECS::StaticMeshComponent mesh;
				if (scene.Read(entity, mesh))
				{
					mesh.Tint = state.Active ? Vec4{.1f, 1, .15f, 1} : Vec4{1, .4f, .1f, 1};
					scene.Write(entity, mesh);
				}
				Log(LogLevel::Info, state.Active ? "Switch activated" : "Switch deactivated");
			}
		for (const auto entity : scene.Query<ECS::RotatorComponent, ECS::TransformComponent>())
		{
			ECS::RotatorComponent rotor;
			ECS::TransformComponent transform;
			if (!scene.Read(entity, rotor) || !scene.Read(entity, transform) || attempt.Paused)
				continue;
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
				ECS::SwitchComponent state;
				const bool active = scene.Read(scene.Find(Name(rotor.Switch)), state) && state.Active;
				rotor.Progress = Approach(rotor.Progress, active, delta, rotor.Duration);
				transform.Rotation = rotor.BaseRotation + rotor.Rotation * rotor.Progress;
			}
			scene.Write(entity, rotor);
			scene.Write(entity, transform);
		}
		for (const auto entity : scene.Query<ECS::DoorComponent, ECS::TransformComponent>())
		{
			ECS::DoorComponent door;
			ECS::TransformComponent transform;
			if (!scene.Read(entity, door) || !scene.Read(entity, transform) || attempt.Paused)
				continue;
			bool ready = door.RequiredCount > 0 && door.RequiredCount <= door.RequiredRotators.size();
			for (std::size_t i = 0; ready && i < door.RequiredCount; ++i)
			{
				ECS::RotatorComponent rotor;
				ready = scene.Read(scene.Find(Name(door.RequiredRotators[i])), rotor) && rotor.Progress >= 1;
			}
			const auto before = door.Progress;
			door.Progress = Approach(door.Progress, ready, delta, door.Duration);
			transform.Position = door.ClosedPosition + door.OpenOffset * door.Progress;
			scene.Write(entity, door);
			scene.Write(entity, transform);
			if (before < 1 && door.Progress >= 1)
				Log(LogLevel::Info, "Gate opened");
		}
		if (!attempt.Completed && readyToExit && scene.IsAlive(bindings.Camera))
		{
			const auto camera = scene.GetWorldTransform(bindings.Camera);
			for (const auto entity : scene.Query<ECS::GoalComponent, ECS::TransformComponent>())
			{
				ECS::GoalComponent goal;
				if (!scene.Read(entity, goal))
					continue;
				if (!Name(goal.Door).empty())
				{
					ECS::DoorComponent door;
					if (!scene.Read(scene.Find(Name(goal.Door)), door) || door.Progress < 1)
						continue;
				}
				const auto world = scene.GetWorldTransform(entity);
				if (std::hypot(camera(0, 3) - world(0, 3), camera(1, 3) - world(1, 3),
							   camera(2, 3) - world(2, 3)) > goal.Radius)
					continue;
				attempt.Completed = true;
				ECS::StaticMeshComponent mesh;
				if (scene.Read(entity, mesh))
				{
					mesh.Tint = {.12f, 1, .12f, 1};
					scene.Write(entity, mesh);
				}
				break;
			}
		}
		scene.Write(attemptEntity, attempt);
	}
}

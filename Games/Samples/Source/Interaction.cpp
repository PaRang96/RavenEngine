// The Puzzle and Collection scenes: switches, rotators, doors, pickups and goals. The camera
// reaching an open goal completes the attempt; Space pauses the puzzle in free flight.

#include "SampleSystems.hpp"

#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Platform/Input.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace Samples
{
	namespace
	{
		using namespace Raven;

		float Approach(float progress, bool active, float delta, float duration)
		{
			constexpr float InteractionSpeed = 1.0f;
			return std::clamp(progress + (active ? 1.0f : -1.0f) * delta * InteractionSpeed / duration, 0.0f, 1.0f);
		}

		// Collects picked pickups and reports whether every scene pickup is collected.
		bool UpdatePickups(const SceneAccess& scene, std::span<const PickHit> picks)
		{
			bool changed = false;
			for (const auto& pick : picks)
			{
				auto* pickup = scene.TryGet<PickupComponent>(pick.Entity);
				if (!pickup || pickup->Collected)
					continue;
				pickup->Collected = true;
				if (auto* mesh = scene.TryGet<ECS::StaticMeshComponent>(pick.Entity))
					mesh->Tint = { .25f, .32f, .36f, 1 };
				changed = true;
			}
			std::size_t total = 0;
			std::size_t collected = 0;
			scene.View<const PickupComponent>().Each([&](ECS::Entity, const PickupComponent& pickup)
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

	// The whole puzzle update; InteractionSystem runs it in free flight only.
	void UpdateInteraction(SystemContext& context, ECS::Entity camera, float delta)
	{
		const auto& scene = context.World;
		auto* const attemptState = scene.Single<AttemptComponent>();
		if (!attemptState)
			return;
		auto& attempt = *attemptState;
		const bool hasPickups = scene.Count<PickupComponent>() != 0;
		if (!attempt.Announced)
		{
			if (hasPickups)
				Log(LogLevel::Info, "Collection: left-click the gold pickups, then fly to the cyan exit. R restarts.");
			else if (scene.Count<SwitchComponent>() != 0)
				Log(LogLevel::Info, "Puzzle: left-click both orange switches, wait for the gate to open, then fly to the cyan goal. R restarts.");
			attempt.Announced = true;
		}
		const bool readyToExit = attempt.Completed || !hasPickups || UpdatePickups(scene, context.Picks);
		if (context.Actions.WasPressed(Pause))
			attempt.Paused = !attempt.Paused;
		// Scene flow (GameServices): applied between frames, so this update still finishes.
		if (attempt.Completed && context.Actions.WasPressed(Continue))
			context.Services.RequestNextScene();
		if (!attempt.Completed)
			for (const auto& pick : context.Picks)
			{
				auto* state = scene.TryGet<SwitchComponent>(pick.Entity);
				if (!state)
					continue;
				state->Active = !state->Active;
				++attempt.SwitchActivations;
				if (auto* mesh = scene.TryGet<ECS::StaticMeshComponent>(pick.Entity))
					mesh->Tint = state->Active ? Vec4{ .1f, 1, .15f, 1 } : Vec4{ 1, .4f, .1f, 1 };
				Log(LogLevel::Info, state->Active ? "Switch activated" : "Switch deactivated");
			}
		if (!attempt.Paused)
		{
			scene.View<RotatorComponent, ECS::TransformComponent>().Each(
				[&](ECS::Entity, RotatorComponent& rotor, ECS::TransformComponent& transform)
				{
					if (rotor.Switch.IsNull())
					{
						constexpr float ContinuousRotationSpeed = 1.0f;
						const auto offset = rotor.OffsetRotation + rotor.Rotation * (delta * ContinuousRotationSpeed);
						const float turn = 2.0f * std::numbers::pi_v<float>;
						rotor.OffsetRotation = { std::remainder(offset.X, turn), std::remainder(offset.Y, turn),
							std::remainder(offset.Z, turn) };
						transform.Rotation = rotor.BaseRotation + rotor.OffsetRotation;
					}
					else
					{
						const auto* state = scene.TryGet<const SwitchComponent>(scene.Resolve(rotor.Switch));
						rotor.Progress = Approach(rotor.Progress, state && state->Active, delta, rotor.Duration);
						transform.Rotation = rotor.BaseRotation + rotor.Rotation * rotor.Progress;
					}
				});
			scene.View<DoorComponent, ECS::TransformComponent>().Each(
				[&](ECS::Entity, DoorComponent& door, ECS::TransformComponent& transform)
				{
					bool ready = door.RequiredCount > 0 && door.RequiredCount <= door.RequiredRotators.size();
					for (std::size_t i = 0; ready && i < door.RequiredCount; ++i)
					{
						const auto* rotor = scene.TryGet<const RotatorComponent>(scene.Resolve(door.RequiredRotators[i]));
						ready = rotor && rotor->Progress >= 1;
					}
					const auto before = door.Progress;
					door.Progress = Approach(door.Progress, ready, delta, door.Duration);
					transform.Position = door.ClosedPosition + door.OpenOffset * door.Progress;
					if (before < 1 && door.Progress >= 1)
						Log(LogLevel::Info, "Gate opened");
				});
		}
		if (!attempt.Completed && readyToExit && scene.IsAlive(camera))
		{
			const auto eye = scene.GetWorldTransform(camera);
			scene.View<const GoalComponent, const ECS::TransformComponent>().Each(
				[&](ECS::Entity entity, const GoalComponent& goal, const ECS::TransformComponent&)
				{
					if (!goal.Door.IsNull())
					{
						const auto* door = scene.TryGet<const DoorComponent>(scene.Resolve(goal.Door));
						if (!door || door->Progress < 1)
							return true;
					}
					const auto world = scene.GetWorldTransform(entity);
					if (std::hypot(eye(0, 3) - world(0, 3), eye(1, 3) - world(1, 3), eye(2, 3) - world(2, 3)) > goal.Radius)
						return true;
					attempt.Completed = true;
					if (auto* mesh = scene.TryGet<ECS::StaticMeshComponent>(entity))
						mesh->Tint = { .12f, 1, .12f, 1 };
					return false;
				});
		}
		if (attempt.Completed && !attempt.CompletionNotified)
		{
			attempt.CompletionNotified = true;
			Log(LogLevel::Info, "Goal completed. Enter continues to the next scene; R restarts.");
			scene.View<const DemoComponent>().Each([&](ECS::Entity, const DemoComponent& demo)
			{
				if (demo.WinSound.IsValid())
					context.Services.PlaySound(demo.WinSound);
				return false;
			});
		}
	}

	void InteractionSystem(SystemContext& context)
	{
		if (!FreeFlight(context))
			return;
		// R restarts any scene, also one without a puzzle (the orbit demo resets its camera instead).
		if (context.Actions.WasPressed(Restart))
			context.Services.RequestRestart();
		UpdateInteraction(context, ActiveCamera(context.World), Delta(context));
	}

	// Benchmarks read the first rotor's phase from the frame trace (scripts/Benchmark-Windows3D.py).
	void TraceSystem(SystemContext& context)
	{
		if (const auto* rotor = context.World.TryGet<const ECS::TransformComponent>(context.World.Find("benchmark-rotor-0")))
			context.Services.SetTracePhase(rotor->Rotation.Y);
	}
}

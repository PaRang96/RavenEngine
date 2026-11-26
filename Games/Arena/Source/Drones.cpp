// The drones that roam the arena: spawning, and chasing the craft or wandering between waypoints.

#include "ArenaSystems.hpp"

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"

namespace Arena
{
	void SpawnDrone(SystemContext& context, const ArenaRulesComponent& rules, Vec3 craft)
	{
		auto& commands = context.Commands;
		const auto drone = commands.Create();
		ECS::TransformComponent transform;
		transform.Position = FreePoint(context, rules, craft, 8.0f, 0.65f, 0.7f);
		context.Services.PlaySound(rules.DroneSound, { transform.Position });
		transform.Scale = { 0.8f, 0.8f, 1.2f };
		commands.Add(drone, transform);
		ECS::StaticMeshComponent mesh;
		mesh.Mesh = rules.DroneMesh;
		mesh.Tint = { 0.92f, 0.22f, 0.2f, 1.0f };
		commands.Add(drone, mesh);
		commands.Add(drone, Paint(true));
		ECS::MoverComponent mover;
		mover.Radius = 0.4f;
		mover.HalfHeight = 0.2f;
		mover.MaxSpeed = 6.0f;
		mover.Layer = DroneLayer;
		commands.Add(drone, mover);
		DroneComponent ai;
		ai.Speed = context.Random.Range(3.4f, 4.8f);
		ai.Waypoint = transform.Position;
		commands.Add(drone, ai);
	}

	// Drones chase the craft within range and otherwise wander between random waypoints.
	void DroneSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto* rules = scene.Single<ArenaRulesComponent>();
		const auto* run = scene.Single<ArenaRunComponent>();
		const auto craft = First<CraftComponent>(scene);
		if (!rules || !run || craft.IsNull())
			return;
		const float seconds = StepSeconds(context);
		const Vec3 target = scene.TryGet<const ECS::TransformComponent>(craft)->Position;
		// A short grace at the start of a run, before drones notice the craft.
		const bool hunting = run->State == RunState::Playing && run->Time > 2.0f;
		for (const auto entity : InIdOrder<DroneComponent>(scene))
		{
			auto* drone = scene.TryGet<DroneComponent>(entity);
			auto* mover = scene.TryGet<ECS::MoverComponent>(entity);
			const auto* transform = scene.TryGet<const ECS::TransformComponent>(entity);
			if (!drone || !mover || !transform)
				continue;
			const Vec3 position = transform->Position;
			Vec3 goal = drone->Waypoint;
			if (hunting && FlatLength(target - position) < drone->ChaseRange)
				goal = target;
			else
			{
				drone->Retarget -= seconds;
				if (drone->Retarget <= 0.0f || FlatLength(drone->Waypoint - position) < 1.0f)
				{
					drone->Waypoint = RandomPoint(context, *rules, 1.5f, 0.0f);
					drone->Retarget = context.Random.Range(3.0f, 6.0f);
				}
				goal = drone->Waypoint;
			}
			const Vec3 toward = Flat(goal - position);
			const float distance = FlatLength(toward);
			mover->DesiredVelocity = distance > 0.05f ? toward * (drone->Speed / distance) : Vec3{};
		}
	}
}

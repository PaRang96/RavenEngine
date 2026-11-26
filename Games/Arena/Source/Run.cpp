// The run flow: a run starts with drones and cells, adds drones over time, and restarts a moment
// after a game over. `--swarm` fills the arena with drones and turns off the game over, for the
// character-backend measurement (measurement 3).

#include "ArenaSystems.hpp"

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"

#include <format>

namespace Arena
{
	void RunSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* run = scene.Single<ArenaRunComponent>();
		const auto* rules = scene.Single<ArenaRulesComponent>();
		const auto craft = First<CraftComponent>(scene);
		if (!run || !rules || craft.IsNull())
			return;
		const float seconds = StepSeconds(context);
		const Vec3 craftPosition = scene.TryGet<const ECS::TransformComponent>(craft)->Position;
		switch (run->State)
		{
		case RunState::Starting:
		{
			for (const auto entity : InIdOrder<DroneComponent>(scene))
				context.Commands.Destroy(scene.GetRef(entity));
			for (const auto entity : InIdOrder<CellComponent>(scene))
				context.Commands.Destroy(scene.GetRef(entity));
			const auto& spawn = scene.TryGet<const CraftComponent>(craft)->Spawn;
			auto transform = *scene.TryGet<const ECS::TransformComponent>(craft);
			transform.Position = spawn;
			scene.Write(craft, transform);
			auto mover = *scene.TryGet<const ECS::MoverComponent>(craft);
			mover.Velocity = {};
			mover.DesiredVelocity = {};
			scene.Write(craft, mover);
			const auto* profiles = Profiles(context);
			const auto profile = profiles ? profiles->Get<ArenaProfile>(ProfileFile) : ArenaProfile{};
			const bool swarm = Swarm(context);
			*run = ArenaRunComponent{};
			run->State = RunState::Playing;
			run->Best = profile.BestScore;
			run->NextDrone = rules->DroneInterval;
			run->Drones = swarm ? rules->SwarmDrones : rules->StartDrones;
			for (std::uint32_t index = 0; index < run->Drones; ++index)
				SpawnDrone(context, *rules, spawn);
			for (std::uint32_t index = 0; index < rules->Cells; ++index)
				SpawnCell(context, *rules, spawn);
			Log(LogLevel::Info, swarm ? std::format("Arena swarm: {} drones, no game over", run->Drones) :
				std::format("Arena run started (best {}): WASD or arrows steer, Space jumps, Q/E turn, Esc pauses",
					run->Best));
			break;
		}
		case RunState::Playing:
			run->Time += seconds;
			if (!Swarm(context) && run->Time >= run->NextDrone && run->Drones < rules->MaxDrones)
			{
				SpawnDrone(context, *rules, craftPosition);
				++run->Drones;
				run->NextDrone += rules->DroneInterval;
			}
			break;
		case RunState::Over:
			run->Timer -= seconds;
			if (run->Timer <= 0.0f)
				run->State = RunState::Starting;
			break;
		}
	}
}

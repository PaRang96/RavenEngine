// The run flow: a run starts with drones and cells, adds drones over time, ends when a drone catches
// the craft or the craft falls out of the arena, and restarts a moment later. The run recorder writes
// finished runs to the profile in Update. `--swarm` fills the arena with drones and turns off the
// game over, for the character-backend measurement (measurement 3).

#include "ArenaSystems.hpp"

#include "Raven/Core/Log.hpp"

#include <format>

namespace Arena
{
	namespace
	{
		// Puts the craft back at a spawn point, at rest, with no steering pending.
		void PlaceCraft(const SceneAccess& scene, ECS::Entity craft, Vec3 spawn)
		{
			ECS::TransformComponent transform;
			if (scene.Read(craft, transform))
			{
				transform.Position = spawn;
				scene.Write(craft, transform);
			}
			ECS::MoverComponent mover;
			if (scene.Read(craft, mover))
			{
				mover.Velocity = {};
				mover.DesiredVelocity = {};
				scene.Write(craft, mover);
			}
			if (auto* input = scene.TryGet<CraftInputComponent>(craft))
				*input = CraftInputComponent{};
		}
	}

	void EndRun(SystemContext& context, ArenaRunComponent& run, RunEnd end)
	{
		run.State = RunState::Over;
		run.Timer = 2.5f;
		run.End = end;
		run.Unrecorded = true;
		if (const auto* rules = context.World.Single<ArenaRulesComponent>())
			context.Services.PlaySound(rules->CaughtSound);
		const auto* how = end == RunEnd::FellOut ? "Fell out of the arena" : "Caught by a drone";
		Log(LogLevel::Info, IsNewBest(run) ? std::format("{}: score {}, a new best", how, run.Score) :
			std::format("{}: score {} (best {})", how, run.Score, run.Best));
	}

	void RunSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* run = scene.Single<ArenaRunComponent>();
		const auto* rules = scene.Single<ArenaRulesComponent>();
		const auto craft = FindCraft(scene);
		if (!run || !rules || !craft)
			return;
		const float seconds = StepSeconds(context);
		const Vec3 craftPosition = craft->Transform->Position;
		const Vec3 spawn = craft->Craft->Spawn;
		switch (run->State)
		{
		case RunState::Starting:
		{
			for (const auto entity : InIdOrder<DroneComponent>(scene))
				context.Commands.Destroy(scene.GetRef(entity));
			for (const auto entity : InIdOrder<CellComponent>(scene))
				context.Commands.Destroy(scene.GetRef(entity));
			PlaceCraft(scene, craft->Entity, spawn);
			// The best so far comes from the runs before, not from user data, so fixed steps stay a
			// function of their inputs (FD-48); the run recorder raises it to the profile's best.
			const std::uint32_t best = std::max(run->Best, run->Score);
			const bool swarm = Swarm(context);
			*run = ArenaRunComponent{};
			run->State = RunState::Playing;
			run->Best = best;
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
			if (craftPosition.Z < rules->FallOutZ)
			{
				// The swarm has no game over, so the craft comes back instead.
				if (Swarm(context))
					PlaceCraft(scene, craft->Entity, spawn);
				else
					EndRun(context, *run, RunEnd::FellOut);
				break;
			}
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

	// Writes each finished run to the profile, outside the fixed steps (FD-33): the run count, and
	// the score when it beats the best. Between runs it raises the run's best to the profile's, so a
	// new session starts from the player's record.
	void RunRecordSystem(SystemContext& context)
	{
		auto* run = context.World.Single<ArenaRunComponent>();
		if (!run)
			return;
		auto* profiles = Profiles(context);
		if (!profiles)
		{
			run->Unrecorded = false;
			return;
		}
		auto profile = profiles->Get<ArenaProfile>(ProfileFile);
		if (run->Unrecorded)
		{
			run->Unrecorded = false;
			++profile.Runs;
			if (IsNewBest(*run))
				profile.BestScore = run->Score;
			profiles->Set(ProfileFile, profile);
			profiles->Save(ProfileFile);
		}
		// Not while a run is over: its banner compares the score with the best before the run.
		else if (run->State != RunState::Over && run->Best < profile.BestScore)
			run->Best = profile.BestScore;
	}
}

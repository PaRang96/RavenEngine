// The player's craft: the controller that turns the steering keys into commands, the fixed step
// that steers the mover, and the drone hit that ends a run.

#include "ArenaSystems.hpp"

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"

#include <format>

namespace Arena
{
	// Turns the steering keys into a CraftCommand on the floor, relative to the camera.
	void CraftControlSystem(SystemContext& context)
	{
		if (context.Services.IsSimulationPaused())
			return;
		const auto* rig = context.World.TryGet<const FollowCameraComponent>(First<FollowCameraComponent>(context.World));
		const float yaw = rig ? rig->Yaw : 0.0f;
		const Vec3 forward = Forward(yaw);
		const Vec3 right{ forward.Y, -forward.X, 0.0f };
		Vec3 move = right * context.Actions.GetAxis(MoveX) + forward * context.Actions.GetAxis(MoveY);
		const float length = FlatLength(move);
		if (length > 1.0f)
			move = move * (1.0f / length);
		CraftCommand command;
		command.Move = move;
		command.Jump = context.Actions.WasPressed(Jump);
		context.Stream.Emit(command);
	}

	// Steers the craft from this tick's records: the latest direction, and a jump if any record had one.
	void CraftSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto* run = scene.Single<ArenaRunComponent>();
		Vec3 move{};
		bool jump = false;
		context.Stream.ForEach<CraftCommand>([&](const CraftCommand& command, std::uint8_t)
		{
			move = Flat(command.Move);
			jump = jump || command.Jump;
		});
		if (!run || run->State != RunState::Playing)
		{
			move = {};
			jump = false;
		}
		const auto* rules = scene.Single<ArenaRulesComponent>();
		scene.View<const CraftComponent, ECS::MoverComponent, const ECS::TransformComponent>().Each(
			[&](ECS::Entity, const CraftComponent& craft, ECS::MoverComponent& mover, const ECS::TransformComponent& transform)
			{
				mover.DesiredVelocity = move * craft.Speed;
				if (jump)
				{
					mover.DesiredVelocity.Z = craft.JumpSpeed;
					if (rules)
						context.Services.PlaySound(rules->JumpSound, { transform.Position });
				}
			});
	}

	// A drone touching the craft ends the run, and a better score goes into the profile.
	void HitSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* run = scene.Single<ArenaRunComponent>();
		const auto craft = First<CraftComponent>(scene);
		if (!run || craft.IsNull() || run->State != RunState::Playing || run->Time <= 0.0f || Swarm(context))
			return;
		const auto* mover = scene.TryGet<const ECS::MoverComponent>(craft);
		if (!mover)
			return;
		PhysicsShape probe;
		probe.Shape = ECS::ColliderShape::Capsule;
		probe.Size = { mover->Radius + 0.08f, mover->Radius + 0.08f, mover->HalfHeight };
		probe.Position = scene.TryGet<const ECS::TransformComponent>(craft)->Position;
		if (context.Physics.Overlap(probe, 1u << DroneLayer).empty())
			return;
		run->State = RunState::Over;
		run->Timer = 2.5f;
		if (const auto* rules = scene.Single<ArenaRulesComponent>())
			context.Services.PlaySound(rules->CaughtSound);
		auto* profiles = Profiles(context);
		auto profile = profiles ? profiles->Get<ArenaProfile>(ProfileFile) : ArenaProfile{};
		++profile.Runs;
		const bool best = run->Score > profile.BestScore;
		if (best)
			profile.BestScore = run->Best = run->Score;
		if (profiles)
		{
			profiles->Set(ProfileFile, profile);
			profiles->Save(ProfileFile);
		}
		Log(LogLevel::Info, best ? std::format("Caught by a drone: score {}, a new best", run->Score) :
			std::format("Caught by a drone: score {} (best {})", run->Score, profile.BestScore));
	}
}

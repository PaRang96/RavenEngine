// The player's craft: the controller that turns the steering keys into commands, the fixed step
// that steers the mover from the latched input, and the drone hit that ends a run.

#include "ArenaSystems.hpp"

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

	// Latches this tick's records into the craft's input, then steers from the input. A frame's
	// records reach only its first fixed step (InputStream.hpp), so steering from the records alone
	// would stop the craft on catch-up steps and slow it below 60 FPS.
	void CraftSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto* run = scene.Single<ArenaRunComponent>();
		bool received = false;
		Vec3 move{};
		bool jump = false;
		context.Stream.ForEach<CraftCommand>([&](const CraftCommand& command, std::uint8_t)
		{
			received = true;
			move = Flat(command.Move);
			jump = jump || command.Jump;
		});
		const bool playing = run && run->State == RunState::Playing;
		const auto* rules = scene.Single<ArenaRulesComponent>();
		bool latched = false;
		scene.View<const CraftComponent, CraftInputComponent, ECS::MoverComponent, const ECS::TransformComponent>().Each(
			[&](ECS::Entity, const CraftComponent& craft, CraftInputComponent& input, ECS::MoverComponent& mover,
				const ECS::TransformComponent& transform)
			{
				latched = true;
				if (received)
				{
					input.Move = move;
					input.Jump = input.Jump || jump;
				}
				if (!playing)
					input = CraftInputComponent{};
				mover.DesiredVelocity = input.Move * craft.Speed;
				if (input.Jump)
				{
					input.Jump = false;
					mover.DesiredVelocity.Z = craft.JumpSpeed;
					if (rules)
						context.Services.PlaySound(rules->JumpSound, { transform.Position });
				}
			});
		// A craft without its latched input (an older scene) gets one, and steers from the next step.
		if (!latched)
			if (const auto craft = First<CraftComponent>(scene); !craft.IsNull() && !scene.TryGet<const CraftInputComponent>(craft))
				context.Commands.Add(scene.GetRef(craft), CraftInputComponent{});
	}

	// A drone touching the craft ends the run.
	void HitSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* run = scene.Single<ArenaRunComponent>();
		const auto craft = FindCraft(scene);
		if (!run || !craft || run->State != RunState::Playing || run->Time <= 0.0f || Swarm(context))
			return;
		PhysicsShape probe;
		probe.Shape = ECS::ColliderShape::Capsule;
		probe.Size = { craft->Mover->Radius + 0.08f, craft->Mover->Radius + 0.08f, craft->Mover->HalfHeight };
		probe.Position = craft->Transform->Position;
		if (!context.Physics.Overlap(probe, 1u << DroneLayer).empty())
			EndRun(context, *run, RunEnd::Caught);
	}
}

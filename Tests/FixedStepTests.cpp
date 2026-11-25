// Guards the fixed-step services of docs/GameServicesDesign.md (FD-34, FD-47, FD-48): input-stream
// delivery by tick, catch-up limits and dropped time, render interpolation, Euler continuity, and
// the seeded random numbers and runtime IDs that level-1 determinism rests on. Physics (C4) and the
// movement backends (C6) write transforms through these paths.

#include "../RavenEngine/Private/Core/FixedGameplayStep.hpp"
#include "../RavenEngine/Private/Scene/TransformInterpolator.hpp"
#include "Raven/Core/SimulationRandom.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Math/Transform.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Support/Check.hpp"

#include <cmath>
#include <vector>

namespace RavenTests
{
	using namespace Raven;

	struct MoveOrder : ECS::Component
	{
		ECS::EntityRef Unit;
		Vec3 Target{};
		RAVEN_COMPONENT(RavenTests::MoveOrder, Unit, Target);
	};

	struct BadOrder : ECS::Component
	{
		ECS::Entity Unit;
		RAVEN_COMPONENT(RavenTests::BadOrder, Unit);
	};

	std::vector<float> Targets(const InputStream& stream)
	{
		std::vector<float> targets;
		stream.ForEach<MoveOrder>([&](const MoveOrder& order, std::uint8_t) { targets.push_back(order.Target.X); });
		return targets;
	}

	void StreamDelivery()
	{
		FixedGameplayStep step(60.0);
		InputStream& stream = step;
		// Emitted before the steps (the Control phase): reaches the frame's first step only.
		stream.Emit(MoveOrder{ {}, {}, { 1.0f, 0.0f, 0.0f } });
		stream.Emit(MoveOrder{ {}, {}, { 2.0f, 0.0f, 0.0f } }, 1);
		RAVEN_CHECK_THROWS(stream.Emit(BadOrder{}));
		step.BeginFrame({}, {}, 2.0 / 60.0);
		RAVEN_CHECK(step.HasStep());
		step.BeginStep();
		RAVEN_CHECK((Targets(stream) == std::vector<float>{ 1.0f, 2.0f }));
		RAVEN_CHECK(stream.GetTick() == 0);
		// Emitted during a step: the next tick.
		stream.Emit(MoveOrder{ {}, {}, { 3.0f, 0.0f, 0.0f } });
		step.ConsumeStep();
		RAVEN_CHECK(step.HasStep());
		step.BeginStep();
		RAVEN_CHECK((Targets(stream) == std::vector<float>{ 3.0f }));
		step.ConsumeStep();
		RAVEN_CHECK(!step.HasStep() && stream.GetTick() == 2 && Targets(stream).empty());

		// A module reload drops waiting records.
		stream.Emit(MoveOrder{ {}, {}, { 4.0f, 0.0f, 0.0f } });
		step.DropRecords();
		step.BeginFrame({}, {}, 1.0 / 60.0);
		step.BeginStep();
		RAVEN_CHECK(Targets(stream).empty());
		step.ConsumeStep();
	}

	void ActionsAndCatchUp()
	{
		FixedGameplayStep step(60.0);
		const ActionSample jump{ ActionId("Jump"), true, true, false, 0.0f };
		// A press in a frame without a step waits for the next step.
		step.BeginFrame({}, std::span(&jump, 1), 0.004);
		RAVEN_CHECK(!step.HasStep());
		const ActionSample held{ ActionId("Jump"), true, false, false, 0.0f };
		step.BeginFrame({}, std::span(&held, 1), 0.016);
		RAVEN_CHECK(step.HasStep() && step.GetActions()[0].Pressed);
		step.BeginStep();
		step.ConsumeStep();
		RAVEN_CHECK(!step.GetActions()[0].Pressed && step.GetActions()[0].Down);
		RAVEN_CHECK(step.GetAlpha() >= 0.0 && step.GetAlpha() < 1.0);

		// A long stall catches up at most four 60 Hz steps; the rest is counted as dropped.
		step.TakeDroppedSeconds();
		step.BeginFrame({}, {}, 1.0);
		int steps = 0;
		while (step.HasStep())
		{
			step.BeginStep();
			step.ConsumeStep();
			++steps;
		}
		RAVEN_CHECK(steps == 4);
		RAVEN_CHECK(std::abs(step.TakeDroppedSeconds() - (1.0 + step.GetStepSeconds() * step.GetAlpha() -
			FixedGameplayStep::MaxCatchUpSeconds)) < 0.02);

		// The rate is a project setting within bounds.
		RAVEN_CHECK_THROWS(FixedGameplayStep(5.0));
		FixedGameplayStep slow(20.0);
		slow.BeginFrame({}, {}, 0.2);
		int slowSteps = 0;
		while (slow.HasStep())
		{
			slow.BeginStep();
			slow.ConsumeStep();
			++slowSteps;
		}
		RAVEN_CHECK(slowSteps == 1);
	}

	void EulerContinuity()
	{
		const Vec3 angles{ 0.3f, -0.7f, 2.9f };
		const Vec3 back = EulerFromQuat(QuatFromEuler(angles), angles);
		RAVEN_CHECK(std::abs(back.X - angles.X) < 1e-4f && std::abs(back.Y - angles.Y) < 1e-4f &&
			std::abs(back.Z - angles.Z) < 1e-4f);
		// Past +pi the stored angle keeps counting instead of jumping to -pi.
		const Vec3 turned = EulerFromQuat(QuatFromEuler({ 0.0f, 0.0f, 3.3f }), { 0.0f, 0.0f, 3.1f });
		RAVEN_CHECK(std::abs(turned.Z - 3.3f) < 1e-4f);
	}

	void Interpolation()
	{
		ECS::Registry registry;
		Scene scene(registry);
		const auto entity = scene.CreateEntity("craft");
		auto access = scene.GetAccess();
		RAVEN_CHECK(registry.Add(entity, ECS::TransformComponent{}));
		RAVEN_CHECK(registry.Add(entity, ECS::InterpolateComponent{}));

		TransformInterpolator interpolator;
		interpolator.BeforeStep(scene);
		ECS::TransformComponent moved;
		moved.Position = { 10.0f, 0.0f, 0.0f };
		access.Write(entity, moved);
		interpolator.AfterSteps(scene);

		RAVEN_CHECK(interpolator.Apply(scene, 0.25f));
		RAVEN_CHECK(std::abs(access.TryGet<const ECS::TransformComponent>(entity)->Position.X - 2.5f) < 1e-5f);
		interpolator.Restore(scene);
		RAVEN_CHECK(access.TryGet<const ECS::TransformComponent>(entity)->Position.X == 10.0f);

		// A move after the steps (a teleport in Update) is drawn where it is.
		moved.Position = { 50.0f, 0.0f, 0.0f };
		access.Write(entity, moved);
		RAVEN_CHECK(!interpolator.Apply(scene, 0.5f));
		RAVEN_CHECK(access.TryGet<const ECS::TransformComponent>(entity)->Position.X == 50.0f);
	}

	void SeededSimulation()
	{
		SimulationRandom first(42);
		SimulationRandom second(42);
		for (int index = 0; index < 100; ++index)
			RAVEN_CHECK(first.NextU64() == second.NextU64());
		const float value = first.Range(-2.0f, 3.0f);
		RAVEN_CHECK(value >= -2.0f && value < 3.0f);
		RAVEN_CHECK(first.RangeInt(4, 4) == 4);

		// Runtime IDs follow the seed, so two runs spawn the same IDs.
		ECS::Registry registry;
		Scene left(registry);
		Scene right(registry);
		left.SeedIds(7);
		right.SeedIds(7);
		for (int index = 0; index < 10; ++index)
			RAVEN_CHECK(left.NewRef().Id == right.NewRef().Id);
	}
}

int main()
{
	return RavenTests::Run("FixedStepTests", []
	{
		RavenTests::StreamDelivery();
		RavenTests::ActionsAndCatchUp();
		RavenTests::EulerContinuity();
		RavenTests::Interpolation();
		RavenTests::SeededSimulation();
	});
}

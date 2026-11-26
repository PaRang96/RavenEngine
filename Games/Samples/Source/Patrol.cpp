// Patrolling movers (fixed step): each steers toward the next point of its circle (FD-47). The
// engine moves them, so they slide along walls, climb the ramp and push crates.

#include "SampleSystems.hpp"

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/TransformComponent.hpp"

#include <cmath>
#include <numbers>

namespace Samples
{
	void PatrolSystem(SystemContext& context)
	{
		const float delta = Delta(context);
		if (delta <= 0.0f)
			return;
		context.World.View<PatrolComponent, ECS::MoverComponent, const ECS::TransformComponent>().Each(
			[delta](ECS::Entity, PatrolComponent& patrol, ECS::MoverComponent& mover, const ECS::TransformComponent& transform)
			{
				patrol.Angle = std::remainder(patrol.Angle + patrol.Speed / std::max(patrol.Radius, 0.1f) * delta,
					2.0f * std::numbers::pi_v<float>);
				const Vec3 target{ patrol.Center.X + patrol.Radius * std::cos(patrol.Angle),
					patrol.Center.Y + patrol.Radius * std::sin(patrol.Angle), 0.0f };
				const Vec3 toward{ target.X - transform.Position.X, target.Y - transform.Position.Y, 0.0f };
				const float distance = std::sqrt(Dot(toward, toward));
				const float speed = std::abs(patrol.Speed) * std::min(1.5f, distance + 0.5f);
				mover.DesiredVelocity = distance > 1e-4f ? toward * (speed / distance) : Vec3{};
			});
	}
}

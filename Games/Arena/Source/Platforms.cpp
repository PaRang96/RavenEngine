// The moving platforms: each eases back and forth between two points over its period.

#include "ArenaSystems.hpp"

#include "Raven/Components/TransformComponent.hpp"

namespace Arena
{
	void PlatformSystem(SystemContext& context)
	{
		const float seconds = StepSeconds(context);
		context.World.View<PlatformComponent, ECS::TransformComponent>().Each(
			[seconds](ECS::Entity, PlatformComponent& platform, ECS::TransformComponent& transform)
			{
				const float period = std::max(platform.Period, 0.5f);
				platform.Time = std::fmod(platform.Time + seconds, period);
				const float blend = 0.5f - 0.5f * Math::Cos(platform.Time / period * 6.2831853f);
				transform.Position = platform.From + (platform.To - platform.From) * blend;
			});
	}
}

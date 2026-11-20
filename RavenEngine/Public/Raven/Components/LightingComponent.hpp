#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Renderer/SceneLighting.hpp"

namespace Raven::ECS
{
	struct LightingComponent : Component
	{
		Vec3 Ambient{ 1.0f, 1.0f, 1.0f };
		Vec3 Direction{ 0.0f, 0.0f, -1.0f };
		Vec3 Color{};

		SceneLighting GetLighting() const { return { Ambient, Direction, Color }; }
		RAVEN_COMPONENT(Raven::ECS::LightingComponent, Ambient, Direction, Color);
	};
}

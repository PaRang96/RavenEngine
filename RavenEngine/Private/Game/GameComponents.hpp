#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

namespace Raven::SampleGame
{
	struct Position : ECS::Component
	{
		Vec3 Location{};

		RAVEN_COMPONENT(Raven::SampleGame::Position, Location);
	};

	struct Velocity : ECS::Component
	{
		Vec3 Linear{};

		RAVEN_COMPONENT(Raven::SampleGame::Velocity, Linear);
	};
}

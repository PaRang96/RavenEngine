#pragma once

#include "Raven/ECS/Entity.hpp"

namespace Raven
{
	// A left-click hit on the presented frame (SystemContext::Picks). Picking runs on Jolt against the
	// static meshes as drawn (docs/GameServicesDesign.md, FD-34; docs/Physics.md#picking).
	struct PickHit
	{
		ECS::Entity Entity;
		// Along the normalized pick ray, in meters.
		float Distance = 0.0f;
	};
}

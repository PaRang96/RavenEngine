#pragma once

#include "Raven/Core/GameSystems.hpp"

namespace Samples
{
	// Switches, rotators, doors, pickups and goals of the Puzzle and Collection scenes. The camera
	// reaching an open goal completes the attempt.
	void UpdateInteraction(Raven::SystemContext& context, Raven::ECS::Entity camera, float delta);
}

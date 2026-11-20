#pragma once
#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Platform/Input.hpp"
namespace Raven::SampleGame
{
	void UpdateInteractionGame(GameBindings bindings, const InputState& input, float delta);
}

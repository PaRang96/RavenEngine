#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/Platform/Input.hpp"

#include <string_view>

namespace Raven
{
	class InputRouter;
	class UserDataStore;

	// One saved binding slot (docs/GameServicesDesign.md, FD-29, FD-33). The bindings file stores one
	// record per slot of every action whose bindings differ from its defaults, keyed
	// "<context>/<action>#<slot>", in the text form so people can read and fix it.
	struct InputBindingRecord : ECS::Component
	{
		InputDevice Device = InputDevice::Keyboard;
		std::uint16_t Code = 0;
		Modifier Modifiers = Modifier::None;
		float Scale = 1.0f;

		RAVEN_COMPONENT(Raven::InputBindingRecord, Device, Code, Modifiers, Scale);
	};

	inline constexpr std::string_view BindingsFile = "Bindings";

	// Applies saved bindings to the router's current contexts. Entries for actions that no longer
	// exist are logged and skipped.
	RAVEN_API void LoadBindings(InputRouter& router, const UserDataStore& store);
	// Rewrites the bindings file from the router's rebound actions, in memory; the caller saves it.
	RAVEN_API void StoreBindings(const InputRouter& router, UserDataStore& store);
}

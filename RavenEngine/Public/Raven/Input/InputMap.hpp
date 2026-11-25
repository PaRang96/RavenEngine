#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Platform/Input.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Raven
{
	// Input mapping contexts (docs/GameServicesDesign.md, FD-29). Games name actions and bind them
	// to physical inputs inside contexts. Active contexts are evaluated in priority order: the
	// engine's developer context first, then key listening, then the game's contexts from the
	// highest priority down. The first context that binds an input consumes it, so lower
	// contexts and the game's raw input view no longer see it.

	// An action, named by text. The name is hashed; saved bindings keep the text.
	struct ActionId
	{
		std::uint64_t Value = 0;

		constexpr ActionId() noexcept = default;
		constexpr explicit ActionId(std::string_view name) noexcept
			: Value(ECS::Detail::HashText(name))
		{
		}
		constexpr bool operator==(const ActionId&) const noexcept = default;
	};

	enum class ActionKind : std::uint8_t
	{
		// Down, pressed and released.
		Button,
		// The sum of each held binding's Scale, clamped to [-1, 1]. Also reports Down while
		// any binding is held.
		Axis,
	};

	struct InputBinding
	{
		InputCode Code{};
		// Modifiers that must be held. Among the bindings of one context that match a press,
		// the one requiring the most modifiers wins, so Shift+F8 and F8 can both be bound.
		Modifier Modifiers = Modifier::None;
		// Contribution to an Axis action; ignored by buttons.
		float Scale = 1.0f;

		constexpr bool operator==(const InputBinding&) const noexcept = default;
	};

	// A captured input: what key listening returns, and what a rebinding stores.
	struct InputChord
	{
		InputCode Code{};
		Modifier Modifiers = Modifier::None;

		constexpr bool operator==(const InputChord&) const noexcept = default;
	};

	struct InputContextDesc
	{
		// Unique within the game.
		std::string_view Name;
		// Higher runs first among the game's contexts; equal priorities keep registration order.
		std::int32_t Priority = 0;
		// While active, consumes every input it does not bind as well (for example a pause menu).
		bool Blocking = false;
		// Active when first registered. A context that already exists keeps its current state
		// across module reloads.
		bool Active = true;
	};

	struct ActionDesc
	{
		std::string_view Context;
		// Unique within its context.
		std::string_view Name;
		ActionKind Kind = ActionKind::Button;
		// Default bindings. Rebinding replaces one slot; ResetBindings restores these.
		std::span<const InputBinding> Bindings;
	};

	// Host copies of a module's registrations, made while the module is loaded.
	struct RegisteredAction
	{
		std::string Context;
		std::string Name;
		ActionKind Kind = ActionKind::Button;
		std::vector<InputBinding> Bindings;
	};

	struct RegisteredInputContext
	{
		std::string Name;
		std::int32_t Priority = 0;
		bool Blocking = false;
		bool Active = true;
	};

	// The state of one action over one frame or one fixed step.
	struct ActionSample
	{
		ActionId Id{};
		bool Down = false;
		bool Pressed = false;
		bool Released = false;
		float Axis = 0.0f;
	};

	// The game's view of the mapped input, borrowed for one system call. Implemented by the engine;
	// everything it keeps (active contexts, rebindings, listening) is host state that survives
	// module reloads.
	class InputActions
	{
	public:
		// Combined over every context that binds the action and received input this frame or step.
		virtual ActionSample Get(ActionId action) const noexcept = 0;
		bool IsDown(ActionId action) const noexcept { return Get(action).Down; }
		bool WasPressed(ActionId action) const noexcept { return Get(action).Pressed; }
		bool WasReleased(ActionId action) const noexcept { return Get(action).Released; }
		float GetAxis(ActionId action) const noexcept { return Get(action).Axis; }

		// Takes effect from the next frame. Unknown names are logged and ignored.
		virtual void SetContextActive(std::string_view context, bool active) = 0;
		virtual bool IsContextActive(std::string_view context) const noexcept = 0;

		// Key listening: the next key or mouse button pressed is captured instead of reaching the
		// game's contexts. A modifier key counts when it is released without another press.
		virtual void BeginListening() = 0;
		virtual void CancelListening() noexcept = 0;
		virtual bool IsListening() const noexcept = 0;
		// The captured input, once; empty while listening or when nothing was captured.
		virtual std::optional<InputChord> TakeListened() noexcept = 0;

		// Replaces binding `slot` of an action (slot == count appends). Returns false for an unknown
		// context or action, an invalid code, or a slot past the end.
		virtual bool Rebind(std::string_view context, std::string_view action, std::size_t slot,
			const InputBinding& binding) = 0;
		// Restores the registered defaults of one context, or of every game context when empty.
		virtual void ResetBindings(std::string_view context = {}) = 0;
		// The current bindings; valid until the next rebinding or module activation.
		virtual std::span<const InputBinding> GetBindings(std::string_view context,
			std::string_view action) const noexcept = 0;

		// A layout-aware label, for example "A" for the Q position on AZERTY. Modifiers are named
		// first: "Shift+F8".
		virtual std::string GetDisplayName(const InputChord& chord) const = 0;

	protected:
		~InputActions() = default;
	};
}

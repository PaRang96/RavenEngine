#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Input/InputMap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Raven
{
	// The engine's developer context (docs/GameServicesDesign.md, FD-29). It exists only in
	// development hosts, is evaluated before everything else, and its keys can be rebound.
	namespace DeveloperInput
	{
		inline constexpr std::string_view Context = "Engine.Developer";
		inline constexpr std::string_view RestartScene = "RestartScene";
		inline constexpr std::string_view NextScene = "NextScene";
		inline constexpr std::string_view ReloadGame = "ReloadGame";
		inline constexpr std::string_view CycleRenderView = "CycleRenderView";
		inline constexpr std::string_view TogglePlay = "TogglePlay";
		// The physics iteration set (docs/GameServicesDesign.md, FD-32).
		inline constexpr std::string_view RestartPlay = "RestartPlay";
		inline constexpr std::string_view StepFixed = "StepFixed";
		inline constexpr std::string_view ToggleColliders = "ToggleColliders";
		// The developer quick-save (docs/GameServicesDesign.md, FD-33).
		inline constexpr std::string_view QuickSave = "QuickSave";
		inline constexpr std::string_view QuickLoad = "QuickLoad";
		// The UI preview mode (docs/UIDesign.md, layer 1): next entry, UI scale, direction, pseudo-locale.
		inline constexpr std::string_view PreviewNext = "PreviewNext";
		inline constexpr std::string_view PreviewScale = "PreviewScale";
		inline constexpr std::string_view PreviewDirection = "PreviewDirection";
		inline constexpr std::string_view PreviewPseudo = "PreviewPseudo";
	}

	// Host-owned input mapping: routes each published input snapshot through the active contexts,
	// and keeps active states, rebindings and key listening across game module reloads.
	class InputRouter final
	{
	public:
		RAVEN_API explicit InputRouter(bool developerContext);

		// Replaces the game's contexts and actions after a module activation. Contexts and actions
		// that still exist keep their active state and rebindings. Inputs held at that moment stay
		// with the game's raw view until released.
		RAVEN_API void SetGame(std::span<const RegisteredInputContext> contexts, std::span<const RegisteredAction> actions);

		// Routes one snapshot. Context activations requested since the last call apply first.
		RAVEN_API void Route(const InputState& raw);

		// The snapshot without the inputs a context or key listening consumed.
		const InputState& GetGameInput() const noexcept { return m_GameInput; }
		// This frame's state of every game action, combined across active contexts.
		std::span<const ActionSample> GetGameActions() const noexcept { return m_GameActions; }
		// Whether a developer action was pressed this frame. False when there is no developer context.
		RAVEN_API bool WasDeveloperPressed(std::string_view action) const noexcept;

		RAVEN_API void SetContextActive(std::string_view context, bool active);
		RAVEN_API bool IsContextActive(std::string_view context) const noexcept;
		RAVEN_API void BeginListening();
		RAVEN_API void CancelListening() noexcept;
		bool IsListening() const noexcept { return m_Listening; }
		RAVEN_API std::optional<InputChord> TakeListened() noexcept;
		RAVEN_API bool Rebind(std::string_view context, std::string_view action, std::size_t slot, const InputBinding& binding);
		RAVEN_API void ResetBindings(std::string_view context);
		RAVEN_API std::span<const InputBinding> GetBindings(std::string_view context, std::string_view action) const noexcept;
		RAVEN_API std::string GetDisplayName(const InputChord& chord) const;

		// Rebound actions, for the bindings file (docs/GameServicesDesign.md, FD-33).
		struct Override
		{
			std::string Context;
			std::string Action;
			std::vector<InputBinding> Bindings;
		};
		RAVEN_API std::vector<Override> GetOverrides() const;
		// Replaces every binding of an action. False for an unknown action or an invalid binding.
		RAVEN_API bool SetBindings(std::string_view context, std::string_view action, std::span<const InputBinding> bindings);
		// Changes with every rebinding, reset or SetBindings, so the host knows when to save.
		std::uint64_t GetBindingRevision() const noexcept { return m_BindingRevision; }

	private:
		struct Action
		{
			ActionId Id;
			std::string Name;
			ActionKind Kind = ActionKind::Button;
			std::vector<InputBinding> Defaults;
			std::vector<InputBinding> Bindings;
			// Routed inputs currently held for this action, and this frame's state.
			std::uint32_t Held = 0;
			ActionSample Sample;
		};
		struct Context
		{
			std::string Name;
			std::int32_t Priority = 0;
			bool Blocking = false;
			bool Active = true;
			bool Requested = true;
			bool Engine = false;
			std::vector<Action> Actions;
		};
		// Where a held input was routed when it was pressed.
		struct Routing
		{
			static constexpr std::int32_t Raw = -1;
			static constexpr std::int32_t Listen = -2;
			std::int32_t Owner = Raw;
			// Actions of the owner context that this input drives, with the binding's scale.
			std::vector<std::pair<std::uint32_t, float>> Actions;
		};

		static constexpr std::size_t CodeCount = InputState::KeyCount + InputState::MouseButtonCount;
		static std::size_t Index(InputCode code) noexcept;

		Context* FindContext(std::string_view name) noexcept;
		const Context* FindContext(std::string_view name) const noexcept;
		void Press(const InputEvent& event, bool& consumed);
		void Release(const InputEvent& event, bool& consumed);
		// Forgets every held routing, without Released transitions.
		void ClearRoutes() noexcept;
		void SortContexts();

		std::vector<Context> m_Contexts;
		std::array<Routing, CodeCount> m_Routes{};
		InputState m_GameInput;
		std::vector<ActionSample> m_GameActions;
		std::uint64_t m_BindingRevision = 0;
		bool m_Listening = false;
		std::optional<InputCode> m_ListenModifier;
		std::optional<InputChord> m_Listened;
	};

	// The InputActions a system receives: the router plus the samples of its frame or fixed step.
	class ActionView final : public InputActions
	{
	public:
		// A read-only view serves fixed steps: a tick's actions are part of its record, so context,
		// listening and rebinding calls there are reported once and ignored (AA2-GP-02 step 3).
		ActionView(InputRouter& router, std::span<const ActionSample> samples, bool readOnly = false) noexcept
			: m_Router(router), m_Samples(samples), m_ReadOnly(readOnly)
		{
		}

		ActionSample Get(ActionId action) const noexcept override
		{
			for (const auto& sample : m_Samples)
				if (sample.Id == action)
					return sample;
			return { action };
		}
		void SetContextActive(std::string_view context, bool active) override
		{
			if (!Refused("SetContextActive"))
				m_Router.SetContextActive(context, active);
		}
		bool IsContextActive(std::string_view context) const noexcept override { return m_Router.IsContextActive(context); }
		void BeginListening() override
		{
			if (!Refused("BeginListening"))
				m_Router.BeginListening();
		}
		void CancelListening() noexcept override
		{
			if (!Refused("CancelListening"))
				m_Router.CancelListening();
		}
		bool IsListening() const noexcept override { return m_Router.IsListening(); }
		std::optional<InputChord> TakeListened() noexcept override
		{
			return Refused("TakeListened") ? std::nullopt : m_Router.TakeListened();
		}
		bool Rebind(std::string_view context, std::string_view action, std::size_t slot,
			const InputBinding& binding) override
		{
			return !Refused("Rebind") && m_Router.Rebind(context, action, slot, binding);
		}
		void ResetBindings(std::string_view context) override
		{
			if (!Refused("ResetBindings"))
				m_Router.ResetBindings(context);
		}
		std::span<const InputBinding> GetBindings(std::string_view context,
			std::string_view action) const noexcept override
		{
			return m_Router.GetBindings(context, action);
		}
		std::string GetDisplayName(const InputChord& chord) const override { return m_Router.GetDisplayName(chord); }

	private:
		// True, after reporting once per process, when this view is read-only.
		bool Refused(const char* call) const noexcept
		{
			if (!m_ReadOnly)
				return false;
			static bool reported = false;
			if (!reported)
			{
				reported = true;
				try
				{
					Log(LogLevel::Warning, std::string("A fixed-step system called Actions.") + call +
						"; contexts, listening and bindings change in Control or Update");
				}
				catch (...)
				{
				}
			}
			return true;
		}

		InputRouter& m_Router;
		std::span<const ActionSample> m_Samples;
		bool m_ReadOnly = false;
	};

	// Layout-independent English names, used when the platform has no localized name.
	RAVEN_API std::string_view GetFallbackKeyName(Key key) noexcept;
}

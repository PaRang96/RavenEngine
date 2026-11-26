#include "Input/InputRouter.hpp"

#include "Platform/PlatformKeyNames.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Core/Log.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace Raven
{
	namespace
	{
		int CountModifiers(Modifier modifiers) noexcept
		{
			return std::popcount(static_cast<unsigned>(static_cast<std::uint8_t>(modifiers)));
		}

		bool IsValidBinding(const InputBinding& binding) noexcept
		{
			return binding.Code.IsValid() && std::isfinite(binding.Scale) &&
				(binding.Modifiers & ~(Modifier::Shift | Modifier::Control | Modifier::Alt | Modifier::Super)) == Modifier::None;
		}
	}

	// SystemTable registration (Public/Raven/Core/GameSystems.hpp) copies the module's text here,
	// while the module is loaded, and rejects declarations the router could not route.
	void SystemTable::InputContext(const InputContextDesc& context)
	{
		if (context.Name.empty())
			throw std::invalid_argument("Input context needs a name");
		if (context.Name.starts_with("Engine."))
			throw std::invalid_argument("Input context names starting with 'Engine.' are reserved: " + std::string(context.Name));
		for (const auto& existing : m_InputContexts)
			if (existing.Name == context.Name)
				throw std::invalid_argument("Input context declared twice: " + std::string(context.Name));
		m_InputContexts.push_back({ std::string(context.Name), context.Priority, context.Blocking, context.Active });
	}

	void SystemTable::Action(const ActionDesc& action)
	{
		if (action.Name.empty())
			throw std::invalid_argument("Input action needs a name");
		if (std::none_of(m_InputContexts.begin(), m_InputContexts.end(),
			[&](const RegisteredInputContext& context) { return context.Name == action.Context; }))
			throw std::invalid_argument("Input action '" + std::string(action.Name) + "' names an undeclared context: " +
				std::string(action.Context));
		for (const auto& existing : m_Actions)
			if (existing.Context == action.Context && existing.Name == action.Name)
				throw std::invalid_argument("Input action declared twice: " + std::string(action.Context) + "/" +
					std::string(action.Name));
		if (action.Kind != ActionKind::Button && action.Kind != ActionKind::Axis)
			throw std::invalid_argument("Input action has an unknown kind: " + std::string(action.Name));
		for (const auto& binding : action.Bindings)
			if (!IsValidBinding(binding))
				throw std::invalid_argument("Input action has an invalid binding: " + std::string(action.Name));
		m_Actions.push_back({ std::string(action.Context), std::string(action.Name), action.Kind,
			std::vector<InputBinding>(action.Bindings.begin(), action.Bindings.end()) });
	}

	InputRouter::InputRouter(bool developerContext)
	{
		if (!developerContext)
			return;
		Context context;
		context.Name = DeveloperInput::Context;
		context.Engine = true;
		struct DeveloperAction
		{
			std::string_view Name;
			Key Key;
			Modifier Modifiers = Modifier::None;
		};
		const DeveloperAction actions[]{
			{ DeveloperInput::RestartScene, Key::F3 },
			{ DeveloperInput::NextScene, Key::F4 },
			{ DeveloperInput::ReloadGame, Key::F5 },
			{ DeveloperInput::CycleRenderView, Key::F7 },
			{ DeveloperInput::TogglePlay, Key::F8 },
			// Shift+F8 wins over F8 because it needs more modifiers.
			{ DeveloperInput::RestartPlay, Key::F8, Modifier::Shift },
			{ DeveloperInput::StepFixed, Key::F9 },
			{ DeveloperInput::ToggleColliders, Key::F10 },
			// Ctrl+F5 and Ctrl+F9 win over F5 and F9, as Shift+F8 does over F8.
			{ DeveloperInput::QuickSave, Key::F5, Modifier::Control },
			{ DeveloperInput::QuickLoad, Key::F9, Modifier::Control },
			{ DeveloperInput::PreviewNext, Key::F6 },
			{ DeveloperInput::PreviewScale, Key::F6, Modifier::Shift },
			{ DeveloperInput::PreviewDirection, Key::F6, Modifier::Control },
			{ DeveloperInput::PreviewPseudo, Key::F6, Modifier::Alt } };
		for (const auto& [name, key, modifiers] : actions)
		{
			Action action;
			action.Id = ActionId(name);
			action.Name = name;
			action.Defaults = action.Bindings = { InputBinding{ InputCode::FromKey(key), modifiers } };
			action.Sample.Id = action.Id;
			context.Actions.push_back(std::move(action));
		}
		m_Contexts.push_back(std::move(context));
	}

	std::size_t InputRouter::Index(InputCode code) noexcept
	{
		return code.Device == InputDevice::Mouse ? InputState::KeyCount + code.Value : code.Value;
	}

	InputRouter::Context* InputRouter::FindContext(std::string_view name) noexcept
	{
		const auto found = std::find_if(m_Contexts.begin(), m_Contexts.end(),
			[&](const Context& context) { return context.Name == name; });
		return found == m_Contexts.end() ? nullptr : &*found;
	}

	const InputRouter::Context* InputRouter::FindContext(std::string_view name) const noexcept
	{
		return const_cast<InputRouter*>(this)->FindContext(name);
	}

	void InputRouter::SortContexts()
	{
		// Engine contexts first, then game contexts by descending priority in registration order.
		std::stable_sort(m_Contexts.begin(), m_Contexts.end(), [](const Context& left, const Context& right)
		{
			if (left.Engine != right.Engine)
				return left.Engine;
			return left.Priority > right.Priority;
		});
	}

	void InputRouter::ClearRoutes() noexcept
	{
		for (auto& route : m_Routes)
		{
			route.Owner = Routing::Raw;
			route.Actions.clear();
		}
		for (auto& context : m_Contexts)
			for (auto& action : context.Actions)
				action.Held = 0;
	}

	void InputRouter::SetGame(std::span<const RegisteredInputContext> contexts, std::span<const RegisteredAction> actions)
	{
		std::vector<Context> next;
		for (auto& context : m_Contexts)
			if (context.Engine)
				next.push_back(std::move(context));
		for (const auto& registered : contexts)
		{
			const Context* previous = FindContext(registered.Name);
			Context context;
			context.Name = registered.Name;
			context.Priority = registered.Priority;
			context.Blocking = registered.Blocking;
			context.Active = context.Requested = previous ? previous->Requested : registered.Active;
			for (const auto& registeredAction : actions)
			{
				if (registeredAction.Context != registered.Name)
					continue;
				Action action;
				action.Id = ActionId(registeredAction.Name);
				action.Name = registeredAction.Name;
				action.Kind = registeredAction.Kind;
				action.Defaults = action.Bindings = registeredAction.Bindings;
				action.Sample.Id = action.Id;
				// A rebinding survives the reload while the action keeps its name.
				if (previous)
					for (const auto& old : previous->Actions)
						if (old.Name == action.Name && old.Bindings != old.Defaults)
							action.Bindings = old.Bindings;
				context.Actions.push_back(std::move(action));
			}
			next.push_back(std::move(context));
		}
		m_Contexts = std::move(next);
		SortContexts();
		ClearRoutes();
		// A game default that a developer key takes is never seen in development: say so at every
		// activation, once per action (AA2-GP-12).
		for (const auto& engine : m_Contexts)
		{
			if (!engine.Engine)
				continue;
			for (const auto& context : m_Contexts)
			{
				if (context.Engine)
					continue;
				for (const auto& action : context.Actions)
				{
					const auto taken = std::find_if(engine.Actions.begin(), engine.Actions.end(), [&action](const Action& developer)
					{
						return std::any_of(developer.Bindings.begin(), developer.Bindings.end(), [&action](const InputBinding& key)
						{
							return std::any_of(action.Defaults.begin(), action.Defaults.end(), [&key](const InputBinding& binding)
							{
								return binding.Code == key.Code && binding.Modifiers == key.Modifiers;
							});
						});
					});
					if (taken != engine.Actions.end())
						Log(LogLevel::Warning, std::format("Input action '{}' in '{}' has a default key that the developer action "
							"'{}' takes, so it never fires in development builds; give it another key", action.Name, context.Name,
							taken->Name));
				}
			}
		}
	}

	void InputRouter::Press(const InputEvent& event, bool& consumed)
	{
		auto& route = m_Routes[Index(event.Code)];
		route.Owner = Routing::Raw;
		route.Actions.clear();
		bool listenChecked = false;
		for (std::size_t index = 0; index <= m_Contexts.size(); ++index)
		{
			// Key listening sits below the engine's contexts and above the game's.
			const bool beyondEngine = index == m_Contexts.size() || !m_Contexts[index].Engine;
			if (beyondEngine && !listenChecked)
			{
				listenChecked = true;
				if (m_Listening)
				{
					route.Owner = Routing::Listen;
					consumed = true;
					const Modifier own = GetModifierOfKey(event.Code.GetKey());
					if (own != Modifier::None)
						m_ListenModifier = event.Code;
					else
					{
						m_Listened = InputChord{ event.Code, event.Modifiers & ~own };
						m_Listening = false;
						m_ListenModifier.reset();
					}
					return;
				}
			}
			if (index == m_Contexts.size())
				break;
			auto& context = m_Contexts[index];
			if (!context.Active)
				continue;
			int best = -1;
			for (std::uint32_t actionIndex = 0; actionIndex < context.Actions.size(); ++actionIndex)
				for (const auto& binding : context.Actions[actionIndex].Bindings)
				{
					if (binding.Code != event.Code || (binding.Modifiers & ~event.Modifiers) != Modifier::None)
						continue;
					const int specificity = CountModifiers(binding.Modifiers);
					if (specificity > best)
					{
						best = specificity;
						route.Actions.clear();
					}
					if (specificity == best &&
						std::none_of(route.Actions.begin(), route.Actions.end(),
							[&](const auto& entry) { return entry.first == actionIndex; }))
						route.Actions.emplace_back(actionIndex, binding.Scale);
				}
			if (best >= 0)
			{
				route.Owner = static_cast<std::int32_t>(index);
				consumed = true;
				for (const auto& [actionIndex, scale] : route.Actions)
				{
					auto& action = context.Actions[actionIndex];
					if (action.Held++ == 0)
						action.Sample.Pressed = true;
				}
				return;
			}
			if (context.Blocking)
			{
				route.Owner = static_cast<std::int32_t>(index);
				consumed = true;
				return;
			}
		}
	}

	void InputRouter::Release(const InputEvent& event, bool& consumed)
	{
		auto& route = m_Routes[Index(event.Code)];
		if (route.Owner == Routing::Listen)
		{
			consumed = true;
			if (m_Listening && m_ListenModifier == event.Code)
			{
				m_Listened = InputChord{ event.Code, Modifier::None };
				m_Listening = false;
				m_ListenModifier.reset();
			}
		}
		else if (route.Owner >= 0 && static_cast<std::size_t>(route.Owner) < m_Contexts.size())
		{
			consumed = true;
			auto& context = m_Contexts[static_cast<std::size_t>(route.Owner)];
			for (const auto& [actionIndex, scale] : route.Actions)
			{
				auto& action = context.Actions[actionIndex];
				if (action.Held > 0 && --action.Held == 0)
					action.Sample.Released = true;
			}
		}
		route.Owner = Routing::Raw;
		route.Actions.clear();
	}

	void InputRouter::Route(const InputState& raw)
	{
		for (auto& context : m_Contexts)
		{
			context.Active = context.Requested;
			for (auto& action : context.Actions)
				action.Sample = { action.Id };
		}

		std::array<bool, CodeCount> consumed{};
		for (const auto& event : raw.Events)
		{
			if (!event.Code.IsValid())
				continue;
			bool taken = false;
			switch (event.Transition)
			{
			case InputTransition::Pressed: Press(event, taken); break;
			case InputTransition::Released: Release(event, taken); break;
			case InputTransition::Repeated: taken = m_Routes[Index(event.Code)].Owner != Routing::Raw; break;
			}
			if (taken)
				consumed[Index(event.Code)] = true;
		}

		// Held state and axes come from the inputs each context owns.
		for (std::size_t code = 0; code < CodeCount; ++code)
		{
			const auto& route = m_Routes[code];
			if (route.Owner == Routing::Raw)
				continue;
			consumed[code] = true;
			if (route.Owner < 0)
				continue;
			auto& context = m_Contexts[static_cast<std::size_t>(route.Owner)];
			for (const auto& [actionIndex, scale] : route.Actions)
				context.Actions[actionIndex].Sample.Axis += scale;
		}
		m_GameActions.clear();
		for (auto& context : m_Contexts)
			for (auto& action : context.Actions)
			{
				action.Sample.Down = action.Held > 0;
				action.Sample.Axis = std::clamp(action.Sample.Axis, -1.0f, 1.0f);
				if (context.Engine || !context.Active)
					continue;
				const auto existing = std::find_if(m_GameActions.begin(), m_GameActions.end(),
					[&](const ActionSample& sample) { return sample.Id == action.Id; });
				if (existing == m_GameActions.end())
				{
					m_GameActions.push_back(action.Sample);
					continue;
				}
				existing->Down |= action.Sample.Down;
				existing->Pressed |= action.Sample.Pressed;
				existing->Released |= action.Sample.Released;
				existing->Axis = std::clamp(existing->Axis + action.Sample.Axis, -1.0f, 1.0f);
			}

		m_GameInput = raw;
		for (std::size_t code = 0; code < CodeCount; ++code)
		{
			if (!consumed[code])
				continue;
			if (code < InputState::KeyCount)
				m_GameInput.Keys[code] = {};
			else
				m_GameInput.MouseButtons[code - InputState::KeyCount] = {};
		}
		std::erase_if(m_GameInput.Events, [&](const InputEvent& event)
		{
			return !event.Code.IsValid() || consumed[Index(event.Code)];
		});
	}

	bool InputRouter::WasDeveloperPressed(std::string_view name) const noexcept
	{
		const auto* context = FindContext(DeveloperInput::Context);
		if (!context)
			return false;
		for (const auto& action : context->Actions)
			if (action.Name == name)
				return action.Sample.Pressed;
		return false;
	}

	void InputRouter::SetContextActive(std::string_view name, bool active)
	{
		auto* context = FindContext(name);
		if (!context)
		{
			Log(LogLevel::Warning, "Unknown input context: " + std::string(name));
			return;
		}
		context->Requested = active;
	}

	bool InputRouter::IsContextActive(std::string_view name) const noexcept
	{
		const auto* context = FindContext(name);
		return context && context->Requested;
	}

	void InputRouter::BeginListening()
	{
		m_Listening = true;
		m_ListenModifier.reset();
		m_Listened.reset();
	}

	void InputRouter::CancelListening() noexcept
	{
		m_Listening = false;
		m_ListenModifier.reset();
	}

	std::optional<InputChord> InputRouter::TakeListened() noexcept
	{
		return std::exchange(m_Listened, std::nullopt);
	}

	bool InputRouter::Rebind(std::string_view contextName, std::string_view actionName, std::size_t slot,
		const InputBinding& binding)
	{
		auto* context = FindContext(contextName);
		if (!context || !IsValidBinding(binding))
			return false;
		const auto action = std::find_if(context->Actions.begin(), context->Actions.end(),
			[&](const Action& candidate) { return candidate.Name == actionName; });
		if (action == context->Actions.end() || slot > action->Bindings.size())
			return false;
		if (slot == action->Bindings.size())
			action->Bindings.push_back(binding);
		else
			action->Bindings[slot] = binding;
		ClearRoutes();
		++m_BindingRevision;
		return true;
	}

	bool InputRouter::SetBindings(std::string_view contextName, std::string_view actionName,
		std::span<const InputBinding> bindings)
	{
		auto* context = FindContext(contextName);
		if (!context || !std::all_of(bindings.begin(), bindings.end(), IsValidBinding))
			return false;
		const auto action = std::find_if(context->Actions.begin(), context->Actions.end(),
			[&](const Action& candidate) { return candidate.Name == actionName; });
		if (action == context->Actions.end())
			return false;
		action->Bindings.assign(bindings.begin(), bindings.end());
		ClearRoutes();
		++m_BindingRevision;
		return true;
	}

	std::vector<InputRouter::Override> InputRouter::GetOverrides() const
	{
		std::vector<Override> overrides;
		for (const auto& context : m_Contexts)
			for (const auto& action : context.Actions)
				if (action.Bindings != action.Defaults)
					overrides.push_back({ context.Name, action.Name, action.Bindings });
		return overrides;
	}

	void InputRouter::ResetBindings(std::string_view contextName)
	{
		for (auto& context : m_Contexts)
		{
			if (contextName.empty() ? context.Engine : context.Name != contextName)
				continue;
			for (auto& action : context.Actions)
				action.Bindings = action.Defaults;
		}
		ClearRoutes();
		++m_BindingRevision;
	}

	std::span<const InputBinding> InputRouter::GetBindings(std::string_view contextName,
		std::string_view actionName) const noexcept
	{
		const auto* context = FindContext(contextName);
		if (!context)
			return {};
		for (const auto& action : context->Actions)
			if (action.Name == actionName)
				return action.Bindings;
		return {};
	}

	std::string InputRouter::GetDisplayName(const InputChord& chord) const
	{
		std::string text;
		const std::pair<Modifier, const char*> modifiers[]{
			{ Modifier::Control, "Ctrl+" }, { Modifier::Shift, "Shift+" },
			{ Modifier::Alt, "Alt+" }, { Modifier::Super, "Super+" } };
		for (const auto& [modifier, label] : modifiers)
			if ((chord.Modifiers & modifier) != Modifier::None)
				text += label;
		if (chord.Code.Device == InputDevice::Mouse)
		{
			constexpr const char* buttons[]{ "Left Mouse", "Right Mouse", "Middle Mouse", "Mouse 4", "Mouse 5" };
			text += chord.Code.Value < std::size(buttons) ? buttons[chord.Code.Value] : "Mouse ?";
			return text;
		}
		const Key key = chord.Code.GetKey();
		if (auto native = Platform::GetNativeKeyName(key); !native.empty())
			return text + native;
		return text + std::string(GetFallbackKeyName(key));
	}
}

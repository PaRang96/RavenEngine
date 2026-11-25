// Guards the input mapping rules of docs/GameServicesDesign.md (FD-29): context priority and
// consumption, modifier specificity, blocking contexts, key listening, and rebindings that
// survive a module reload. The input stream (C3) and the UI context (C7) build on these rules.

#include "../RavenEngine/Private/Input/InputRouter.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Support/Check.hpp"

#include <vector>

namespace RavenTests
{
	using namespace Raven;

	InputState Press(InputState state, Key key)
	{
		state.ClearTransientState();
		state.SetKeyDown(key, true);
		return state;
	}

	InputState Release(InputState state, Key key)
	{
		state.ClearTransientState();
		state.SetKeyDown(key, false);
		return state;
	}

	SystemTable GameTable()
	{
		SystemTable table;
		table.InputContext({ "Gameplay", 0 });
		table.InputContext({ "Menu", 10, true, false });
		const InputBinding jump[]{ { InputCode::FromKey(Key::Space) } };
		const InputBinding move[]{ { InputCode::FromKey(Key::W), Modifier::None, 1.0f },
			{ InputCode::FromKey(Key::S), Modifier::None, -1.0f } };
		const InputBinding confirm[]{ { InputCode::FromKey(Key::Enter) } };
		const InputBinding stealF5[]{ { InputCode::FromKey(Key::F5) } };
		table.Action({ "Gameplay", "Jump", ActionKind::Button, jump });
		table.Action({ "Gameplay", "Move", ActionKind::Axis, move });
		table.Action({ "Gameplay", "Taunt", ActionKind::Button, stealF5 });
		table.Action({ "Menu", "Confirm", ActionKind::Button, confirm });
		return table;
	}

	ActionSample Get(const InputRouter& router, std::string_view name)
	{
		for (const auto& sample : router.GetGameActions())
			if (sample.Id == ActionId(name))
				return sample;
		return {};
	}

	void Registration()
	{
		SystemTable table;
		table.InputContext({ "Gameplay" });
		RAVEN_CHECK_THROWS(table.InputContext({ "Gameplay" }));
		RAVEN_CHECK_THROWS(table.InputContext({ "Engine.Mine" }));
		const InputBinding none[]{ { InputCode{ InputDevice::Keyboard, 0 } } };
		RAVEN_CHECK_THROWS(table.Action({ "Gameplay", "Bad", ActionKind::Button, none }));
		RAVEN_CHECK_THROWS(table.Action({ "Missing", "Jump", ActionKind::Button, {} }));
		table.Action({ "Gameplay", "Jump", ActionKind::Button, {} });
		RAVEN_CHECK_THROWS(table.Action({ "Gameplay", "Jump", ActionKind::Button, {} }));
	}

	void ConsumptionAndPriority()
	{
		InputRouter router(true);
		const auto table = GameTable();
		router.SetGame(table.GetInputContexts(), table.GetActions());

		// The developer context comes first: F5 reloads and never reaches the game.
		InputState state = Press({}, Key::F5);
		router.Route(state);
		RAVEN_CHECK(router.WasDeveloperPressed(DeveloperInput::ReloadGame));
		RAVEN_CHECK(!Get(router, "Taunt").Pressed);
		RAVEN_CHECK(!router.GetGameInput().WasKeyPressed(Key::F5));
		state = Release(state, Key::F5);
		router.Route(state);

		// A bound key is consumed from the raw view; an unbound key passes through.
		state = Press(state, Key::Space);
		state.SetKeyDown(Key::Q, true);
		router.Route(state);
		RAVEN_CHECK(Get(router, "Jump").Pressed && Get(router, "Jump").Down);
		RAVEN_CHECK(!router.GetGameInput().IsKeyDown(Key::Space));
		RAVEN_CHECK(router.GetGameInput().WasKeyPressed(Key::Q));
		state = Release(state, Key::Space);
		router.Route(state);
		RAVEN_CHECK(Get(router, "Jump").Released && !Get(router, "Jump").Down);

		// Axes sum their held bindings.
		state = Press(state, Key::W);
		router.Route(state);
		RAVEN_CHECK(Get(router, "Move").Axis == 1.0f);
		state = Press(state, Key::S);
		router.Route(state);
		RAVEN_CHECK(Get(router, "Move").Axis == 0.0f);
		state = Release(Release(state, Key::W), Key::S);
		router.Route(state);

		// An active blocking menu takes everything below it, from the next frame on.
		router.SetContextActive("Menu", true);
		router.Route(Press(state, Key::Enter));
		RAVEN_CHECK(Get(router, "Confirm").Pressed);
		state = Press(Release(Press(state, Key::Enter), Key::Enter), Key::Space);
		router.Route(state);
		RAVEN_CHECK(!Get(router, "Jump").Pressed);
		RAVEN_CHECK(!router.GetGameInput().IsKeyDown(Key::Space));
		// The developer context stays above a blocking game context.
		router.Route(Press(state, Key::F8));
		RAVEN_CHECK(router.WasDeveloperPressed(DeveloperInput::TogglePlay));
	}

	void ModifierSpecificity()
	{
		InputRouter router(false);
		SystemTable table;
		table.InputContext({ "Gameplay" });
		const InputBinding plain[]{ { InputCode::FromKey(Key::F8) } };
		const InputBinding shifted[]{ { InputCode::FromKey(Key::F8), Modifier::Shift } };
		const InputBinding forward[]{ { InputCode::FromKey(Key::W) } };
		table.Action({ "Gameplay", "Pause", ActionKind::Button, plain });
		table.Action({ "Gameplay", "Restart", ActionKind::Button, shifted });
		table.Action({ "Gameplay", "Forward", ActionKind::Button, forward });
		router.SetGame(table.GetInputContexts(), table.GetActions());

		InputState state = Press({}, Key::LeftShift);
		state.SetKeyDown(Key::F8, true);
		router.Route(state);
		RAVEN_CHECK(Get(router, "Restart").Pressed);
		RAVEN_CHECK(!Get(router, "Pause").Pressed);
		// A binding without modifiers still fires while a modifier is held.
		state = Press(state, Key::W);
		router.Route(state);
		RAVEN_CHECK(Get(router, "Forward").Pressed);
	}

	void Listening()
	{
		InputRouter router(true);
		const auto table = GameTable();
		router.SetGame(table.GetInputContexts(), table.GetActions());

		router.BeginListening();
		InputState state = Press({}, Key::LeftControl);
		state.SetKeyDown(Key::Space, true);
		router.Route(state);
		RAVEN_CHECK(!Get(router, "Jump").Pressed);
		const auto chord = router.TakeListened();
		RAVEN_CHECK(chord && chord->Code == InputCode::FromKey(Key::Space) && chord->Modifiers == Modifier::Control);
		RAVEN_CHECK(!router.IsListening() && !router.TakeListened());

		// A modifier key released alone is captured as itself.
		state = Release(Release(state, Key::Space), Key::LeftControl);
		router.Route(state);
		router.BeginListening();
		state = Press(state, Key::RightShift);
		router.Route(state);
		RAVEN_CHECK(router.IsListening());
		state = Release(state, Key::RightShift);
		router.Route(state);
		const auto modifier = router.TakeListened();
		RAVEN_CHECK(modifier && modifier->Code == InputCode::FromKey(Key::RightShift));

		// Developer keys still work while listening.
		router.BeginListening();
		router.Route(Press(state, Key::F7));
		RAVEN_CHECK(router.WasDeveloperPressed(DeveloperInput::CycleRenderView) && router.IsListening());
	}

	void RebindingSurvivesReload()
	{
		InputRouter router(false);
		auto table = GameTable();
		router.SetGame(table.GetInputContexts(), table.GetActions());
		RAVEN_CHECK(router.Rebind("Gameplay", "Jump", 0, { InputCode::FromMouse(MouseButton::Right) }));
		RAVEN_CHECK(!router.Rebind("Gameplay", "Jump", 5, { InputCode::FromKey(Key::J) }));
		RAVEN_CHECK(!router.Rebind("Gameplay", "Nope", 0, { InputCode::FromKey(Key::J) }));
		router.SetContextActive("Menu", true);

		table = GameTable();
		router.SetGame(table.GetInputContexts(), table.GetActions());
		const auto bindings = router.GetBindings("Gameplay", "Jump");
		RAVEN_CHECK(bindings.size() == 1 && bindings[0].Code == InputCode::FromMouse(MouseButton::Right));
		RAVEN_CHECK(router.IsContextActive("Menu"));

		router.ResetBindings("Gameplay");
		RAVEN_CHECK(router.GetBindings("Gameplay", "Jump")[0].Code == InputCode::FromKey(Key::Space));
		RAVEN_CHECK(router.GetDisplayName({ InputCode::FromMouse(MouseButton::Left), Modifier::Shift }) ==
			"Shift+Left Mouse");
		RAVEN_CHECK(!router.GetDisplayName({ InputCode::FromKey(Key::A) }).empty());
	}

	void MouseEventsKeepTheirOrder()
	{
		InputState state;
		state.SetCursorPosition(10, 20);
		state.SetKeyDown(Key::A, true);
		state.PressMouseButton({ MouseButton::Left, 10, 20, 100, 100 });
		state.SetKeyDown(Key::A, false);
		state.SetMouseButtonDown(MouseButton::Left, false);
		RAVEN_CHECK(state.Events.size() == 4);
		RAVEN_CHECK(state.Events[1].Code == InputCode::FromMouse(MouseButton::Left) &&
			state.Events[1].FramebufferWidth == 100);
		RAVEN_CHECK(state.Events[3].Transition == InputTransition::Released && state.Events[3].X == 10);
	}
}

int main()
{
	return RavenTests::Run("InputMappingTests", []
	{
		RavenTests::Registration();
		RavenTests::ConsumptionAndPriority();
		RavenTests::ModifierSpecificity();
		RavenTests::Listening();
		RavenTests::RebindingSurvivesReload();
		RavenTests::MouseEventsKeepTheirOrder();
	});
}

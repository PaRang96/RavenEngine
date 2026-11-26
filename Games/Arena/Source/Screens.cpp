// The systems behind the arena's screens: pausing, the pause menu and its key-binding screen, and
// the heads-up display. ArenaUi.cpp draws the screens from the models these systems fill.

#include "ArenaSystems.hpp"
#include "ArenaUi.hpp"

#include "Raven/Core/Log.hpp"
#include "Raven/Platform/Input.hpp"

namespace Arena
{
	namespace
	{
		// The pause menu's view state, on the camera rig. The menu system adds it when first needed; it
		// exists from the next frame.
		ArenaMenuComponent* Menu(SystemContext& context, bool add = false)
		{
			const auto rig = First<FollowCameraComponent>(context.World);
			if (rig.IsNull())
				return nullptr;
			if (auto* menu = context.World.TryGet<ArenaMenuComponent>(rig))
				return menu;
			if (add)
				context.Commands.Add(context.World.GetRef(rig), ArenaMenuComponent{});
			return nullptr;
		}

		// A menu screen as it opens: nothing listening, and its first button about to take focus.
		ArenaMenuComponent Screen(MenuScreen screen)
		{
			ArenaMenuComponent menu;
			menu.Screen = screen;
			return menu;
		}

		void PlayUiSound(SystemContext& context, bool pause)
		{
			if (const auto* rules = context.World.Single<ArenaRulesComponent>())
				context.Services.PlaySound(pause ? rules->PauseSound : rules->ClickSound);
		}

		void SetPaused(SystemContext& context, bool paused)
		{
			PlayUiSound(context, true);
			context.Services.SetSimulationPaused(paused);
			context.Actions.SetContextActive(PauseContext, paused);
			if (auto* menu = Menu(context))
				*menu = ArenaMenuComponent{};
		}

		// The key-binding screen's rows: each action direction with its keys, from the live bindings.
		struct BindingTarget
		{
			std::string_view Action;
			float Sign = 1.0f;
			const char* Label = "";
		};
		constexpr BindingTarget BindingTargets[]{
			{ "MoveY", 1.0f, "Forward" }, { "MoveY", -1.0f, "Back" }, { "MoveX", -1.0f, "Left" },
			{ "MoveX", 1.0f, "Right" }, { "Jump", 1.0f, "Jump" }, { "Turn", 1.0f, "Turn left" },
			{ "Turn", -1.0f, "Turn right" }, { "Pause", 1.0f, "Pause" } };

		// The binding indices of one row, in order.
		std::vector<std::size_t> RowSlots(InputActions& actions, const BindingTarget& target)
		{
			std::vector<std::size_t> slots;
			const auto bindings = actions.GetBindings(PlayContext, target.Action);
			for (std::size_t index = 0; index < bindings.size(); ++index)
				if ((bindings[index].Scale < 0.0f) == (target.Sign < 0.0f))
					slots.push_back(index);
			return slots;
		}

		std::vector<BindingRow> BindingRows(InputActions& actions)
		{
			std::vector<BindingRow> rows;
			for (const auto& target : BindingTargets)
			{
				BindingRow row{ target.Label, {} };
				const auto bindings = actions.GetBindings(PlayContext, target.Action);
				for (const auto slot : RowSlots(actions, target))
					row.Keys.push_back(actions.GetDisplayName({ bindings[slot].Code, bindings[slot].Modifiers }));
				rows.push_back(std::move(row));
			}
			return rows;
		}
	}

	// Esc pauses: the pause context consumes every gameplay input, and the simulation stops. On the
	// key-binding screen, Esc goes back to the menu instead.
	void PauseSystem(SystemContext& context)
	{
		auto& actions = context.Actions;
		if (!context.Services.IsSimulationPaused() && actions.WasPressed(Pause))
		{
			SetPaused(context, true);
			Log(LogLevel::Info, "Arena paused; Esc resumes");
		}
		else if (context.Services.IsSimulationPaused() && actions.WasPressed(Resume))
		{
			auto* menu = Menu(context);
			if (menu && menu->Screen == MenuScreen::Bindings)
				*menu = Screen(MenuScreen::Main);
			else
				SetPaused(context, false);
		}
	}

	// The pause menu and the key-binding screen, while the game is paused (Control phase).
	void MenuSystem(SystemContext& context)
	{
		if (!context.Services.IsSimulationPaused())
			return;
		auto* menu = Menu(context, true);
		if (!menu)
			return;
		auto& actions = context.Actions;
		const bool focus = std::exchange(menu->FocusPending, false);
		if (menu->Screen == MenuScreen::Main)
		{
			const auto result = DrawPauseMenu(context.Ui, focus);
			if (result.Choice == MenuChoice::Resume)
				SetPaused(context, false);
			else if (result.Choice == MenuChoice::OpenBindings)
			{
				PlayUiSound(context, false);
				*menu = Screen(MenuScreen::Bindings);
			}
			return;
		}

		// A key captured for the row being rebound replaces its binding, or adds one.
		if (menu->Listening >= 0 && !actions.IsListening())
		{
			if (const auto chord = actions.TakeListened())
			{
				const auto& target = BindingTargets[std::min<std::size_t>(static_cast<std::size_t>(menu->Listening),
					std::size(BindingTargets) - 1)];
				const auto slots = RowSlots(actions, target);
				const auto slot = menu->Slot < slots.size() ? slots[menu->Slot] :
					actions.GetBindings(PlayContext, target.Action).size();
				if (chord->Code != InputCode::FromKey(Key::Escape))
					actions.Rebind(PlayContext, target.Action, slot, { chord->Code, chord->Modifiers, target.Sign });
			}
			menu->Listening = -1;
		}
		const auto result = DrawBindings(context.Ui, BindingRows(actions), menu->Listening, focus);
		if (result.Choice != MenuChoice::None)
			PlayUiSound(context, false);
		if (result.Choice == MenuChoice::Back)
			*menu = Screen(MenuScreen::Main);
		else if (result.Choice == MenuChoice::ResetBindings)
			actions.ResetBindings(PlayContext);
		else if (result.Choice == MenuChoice::Rebind)
		{
			menu->Listening = static_cast<std::int32_t>(result.Row);
			menu->Slot = static_cast<std::uint32_t>(result.Slot);
			actions.BeginListening();
		}
	}

	// The heads-up display: score, best, time and drones, and the start and game-over banners.
	void HudSystem(SystemContext& context)
	{
		const auto* run = context.World.Single<ArenaRunComponent>();
		if (!run || Swarm(context))
			return;
		HudModel model;
		model.Score = run->Score;
		model.Best = run->Best;
		model.Drones = run->Drones;
		model.Time = run->Time;
		// The pause menu replaces the banners; the HUD is declared after it and would cover it.
		const bool paused = context.Services.IsSimulationPaused();
		model.Starting = !paused && run->State == RunState::Starting;
		model.Over = !paused && run->State == RunState::Over;
		model.NewBest = model.Over && run->Score > 0 && run->Score >= run->Best;
		DrawHud(context.Ui, model);
	}
}

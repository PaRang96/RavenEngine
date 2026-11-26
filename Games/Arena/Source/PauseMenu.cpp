// Pausing and the pause menu: Esc pauses and the pause context consumes every gameplay input; the
// menu resumes or opens the key-binding screen (Bindings.cpp).

#include "ArenaSystems.hpp"
#include "ArenaUi.hpp"

#include "Raven/Core/Log.hpp"

namespace Arena
{
	ArenaMenuComponent* Menu(SystemContext& context, bool add)
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

	// Nothing listening, and the screen's first button about to take focus.
	ArenaMenuComponent OpenedScreen(MenuScreen screen)
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

	// On the key-binding screen, Esc goes back to the menu instead of resuming.
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
				*menu = OpenedScreen(MenuScreen::Main);
			else
				SetPaused(context, false);
		}
	}

	// The pause menu, or the key-binding screen, while the game is paused (Control phase).
	void MenuSystem(SystemContext& context)
	{
		if (!context.Services.IsSimulationPaused())
			return;
		auto* menu = Menu(context, true);
		if (!menu)
			return;
		const bool focus = std::exchange(menu->FocusPending, false);
		if (menu->Screen == MenuScreen::Bindings)
		{
			BindingsScreen(context, *menu, focus);
			return;
		}
		const auto result = DrawPauseMenu(context.Ui, focus);
		if (result.Choice == MenuChoice::Resume)
			SetPaused(context, false);
		else if (result.Choice == MenuChoice::OpenBindings)
		{
			PlayUiSound(context, false);
			*menu = OpenedScreen(MenuScreen::Bindings);
		}
	}

	MenuResult DrawPauseMenu(Raven::UI::Ui& ui, bool focus)
	{
		MenuResult result;
		ui.Begin("Pause", CenteredPanel());
		ui.Begin("Card", Card(320.0f));
		ui.Text("Title", "Paused", TextStyle(32.0f));
		ui.Text("Hint", "Esc resumes", TextStyle(14.0f, MutedColor));
		if (ui.Button("Resume", "Resume", ButtonStyle()).Clicked)
			result.Choice = MenuChoice::Resume;
		if (ui.Button("Bindings", "Key bindings", ButtonStyle()).Clicked)
			result.Choice = MenuChoice::OpenBindings;
		if (focus)
			ui.Focus("Resume");
		ui.End();
		ui.End();
		return result;
	}
}

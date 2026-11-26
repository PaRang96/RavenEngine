// The heads-up display: score, best, time and drones, and the start and game-over banners.

#include "ArenaSystems.hpp"
#include "ArenaUi.hpp"

#include <format>

namespace Arena
{
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
		model.NewBest = model.Over && IsNewBest(*run);
		model.FellOut = run->End == RunEnd::FellOut;
		DrawHud(context.Ui, model);
	}

	void DrawHud(Raven::UI::Ui& ui, const HudModel& model)
	{
		using namespace Raven::UI;
		Style corner;
		corner.Absolute = true;
		corner.Start = 24.0f;
		corner.Top = 24.0f;
		corner.Padding = Symmetric(16.0f, 12.0f);
		corner.Gap = 2.0f;
		corner.Background = PanelColor;
		corner.CornerRadius = 8.0f;
		ui.Begin("Hud", corner);
		ui.Text("Score", std::format("Score {}", model.Score), TextStyle(28.0f, AccentColor));
		ui.Text("Best", std::format("Best {}", model.Best), TextStyle(16.0f, MutedColor));
		const auto seconds = static_cast<int>(model.Time);
		ui.Text("Time", std::format("Time {}:{:02}", seconds / 60, seconds % 60), TextStyle(16.0f, MutedColor));
		ui.Text("Drones", std::format("Drones {}", model.Drones), TextStyle(16.0f, MutedColor));
		ui.End();

		if (!model.Starting && !model.Over)
			return;
		ui.Begin("Banner", CenteredPanel());
		Style banner;
		banner.Padding = Symmetric(28.0f, 16.0f);
		banner.Background = PanelColor;
		banner.CornerRadius = 10.0f;
		banner.Align = Place::Center;
		banner.Gap = 6.0f;
		ui.Begin("Card", banner);
		if (model.Starting)
			ui.Text("Title", "Get ready", TextStyle(32.0f));
		else
		{
			ui.Text("Title", model.FellOut ? "Fell out of the arena" : "Caught by a drone", TextStyle(32.0f));
			ui.Text("Result", model.NewBest ? std::format("Score {} - a new best!", model.Score) :
				std::format("Score {}   Best {}", model.Score, model.Best),
				TextStyle(18.0f, model.NewBest ? AccentColor : MutedColor));
		}
		ui.End();
		ui.End();
	}
}

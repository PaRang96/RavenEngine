#include "ArenaUi.hpp"

#include <cmath>
#include <format>

namespace Arena
{
	namespace
	{
		using namespace Raven::UI;

		// The arena's look: dark translucent panels, a cyan accent like the energy cells.
		constexpr Color Panel = Rgba(0x0B1118D0);
		constexpr Color Accent = Rgba(0x3FE0FFFF);
		constexpr Color Muted = Rgba(0xA8B4C0FF);
		constexpr Color ButtonFill = Rgba(0x1E2A36FF);

		Style TextStyle(float size, Color color = { 1, 1, 1, 1 })
		{
			Style style;
			style.FontSize = size;
			style.TextColor = color;
			style.Wrap = false;
			return style;
		}

		Style ButtonStyle(float width = Auto)
		{
			Style style;
			style.Padding = Symmetric(18.0f, 10.0f);
			style.Background = ButtonFill;
			style.CornerRadius = 6.0f;
			style.FontSize = 18.0f;
			style.TextAlign = TextAlignment::Center;
			style.Wrap = false;
			style.Width = width;
			return style;
		}

		// Fills the screen and centers its child.
		Style CenteredPanel()
		{
			Style style;
			style.Absolute = true;
			style.Start = 0.0f;
			style.End = 0.0f;
			style.Top = 0.0f;
			style.Bottom = 0.0f;
			style.Justify = Place::Center;
			style.Align = Place::Center;
			return style;
		}

		Style Card(float width)
		{
			Style style;
			style.Width = width;
			style.Padding = All(24.0f);
			style.Gap = 12.0f;
			style.Background = Panel;
			style.CornerRadius = 10.0f;
			style.BorderWidth = 1.0f;
			style.BorderColor = Rgba(0x3FE0FF40);
			return style;
		}
	}

	void DrawHud(Ui& ui, const HudModel& model)
	{
		Style corner;
		corner.Absolute = true;
		corner.Start = 24.0f;
		corner.Top = 24.0f;
		corner.Padding = Symmetric(16.0f, 12.0f);
		corner.Gap = 2.0f;
		corner.Background = Panel;
		corner.CornerRadius = 8.0f;
		ui.Begin("Hud", corner);
		ui.Text("Score", std::format("Score {}", model.Score), TextStyle(28.0f, Accent));
		ui.Text("Best", std::format("Best {}", model.Best), TextStyle(16.0f, Muted));
		const auto seconds = static_cast<int>(model.Time);
		ui.Text("Time", std::format("Time {}:{:02}", seconds / 60, seconds % 60), TextStyle(16.0f, Muted));
		ui.Text("Drones", std::format("Drones {}", model.Drones), TextStyle(16.0f, Muted));
		ui.End();

		if (!model.Starting && !model.Over)
			return;
		ui.Begin("Banner", CenteredPanel());
		Style banner;
		banner.Padding = Symmetric(28.0f, 16.0f);
		banner.Background = Panel;
		banner.CornerRadius = 10.0f;
		banner.Align = Place::Center;
		banner.Gap = 6.0f;
		ui.Begin("Card", banner);
		if (model.Starting)
			ui.Text("Title", "Get ready", TextStyle(32.0f));
		else
		{
			ui.Text("Title", "Caught by a drone", TextStyle(32.0f));
			ui.Text("Result", model.NewBest ? std::format("Score {} - a new best!", model.Score) :
				std::format("Score {}   Best {}", model.Score, model.Best), TextStyle(18.0f, model.NewBest ? Accent : Muted));
		}
		ui.End();
		ui.End();
	}

	MenuResult DrawPauseMenu(Ui& ui, bool focus)
	{
		MenuResult result;
		ui.Begin("Pause", CenteredPanel());
		ui.Begin("Card", Card(320.0f));
		ui.Text("Title", "Paused", TextStyle(32.0f));
		ui.Text("Hint", "Esc resumes", TextStyle(14.0f, Muted));
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

	MenuResult DrawBindings(Ui& ui, const std::vector<BindingRow>& rows, std::int32_t listening, bool focus)
	{
		MenuResult result;
		ui.Begin("Bindings", CenteredPanel());
		ui.Begin("Card", Card(560.0f));
		ui.Text("Title", "Key bindings", TextStyle(28.0f));
		ui.Text("Hint", listening >= 0 ? "Press a key for the highlighted action (Esc keeps it)" :
			"Click a key to change it, or + to add one", TextStyle(14.0f, Muted));
		for (std::size_t row = 0; row < rows.size(); ++row)
		{
			Style line;
			line.Direction = Flow::Row;
			line.Align = Place::Center;
			line.Gap = 8.0f;
			ui.BeginKey("Row", row + 1, line);
			Style label = TextStyle(16.0f, static_cast<std::int32_t>(row) == listening ? Accent : Raven::UI::Color{});
			label.Width = 160.0f;
			ui.Text("Label", rows[row].Label, label);
			for (std::size_t slot = 0; slot < rows[row].Keys.size(); ++slot)
			{
				Style key = ButtonStyle();
				key.FontSize = 15.0f;
				key.Padding = Symmetric(12.0f, 6.0f);
				key.MinWidth = 56.0f;
				if (ui.Button(std::format("Key{}", slot), rows[row].Keys[slot], key).Clicked)
					result = { MenuChoice::Rebind, row, slot };
			}
			Style add = ButtonStyle();
			add.FontSize = 15.0f;
			add.Padding = Symmetric(10.0f, 6.0f);
			add.Background = Rgba(0x141C24FF);
			if (ui.Button("Add", "+", add).Clicked)
				result = { MenuChoice::Rebind, row, rows[row].Keys.size() };
			ui.End();
		}
		Style actions;
		actions.Direction = Flow::Row;
		actions.Gap = 12.0f;
		actions.Justify = Place::End;
		ui.Begin("Actions", actions);
		if (ui.Button("Reset", "Reset to defaults", ButtonStyle()).Clicked)
			result.Choice = MenuChoice::ResetBindings;
		if (ui.Button("Back", "Back", ButtonStyle()).Clicked)
			result.Choice = MenuChoice::Back;
		if (focus)
			ui.Focus("Back");
		ui.End();
		ui.End();
		ui.End();
		return result;
	}
}

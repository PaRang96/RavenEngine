// What the arena's screens share: the look, and the UI preview entries (F6).

#include "ArenaUi.hpp"

namespace Arena
{
	using namespace Raven::UI;

	Style TextStyle(float size, Color color)
	{
		Style style;
		style.FontSize = size;
		style.TextColor = color;
		style.Wrap = false;
		return style;
	}

	Style ButtonStyle(float width)
	{
		Style style;
		style.Padding = Symmetric(18.0f, 10.0f);
		style.Background = ButtonColor;
		style.CornerRadius = 6.0f;
		style.FontSize = 18.0f;
		style.TextAlign = TextAlignment::Center;
		style.Wrap = false;
		style.Width = width;
		return style;
	}

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
		style.Background = PanelColor;
		style.CornerRadius = 10.0f;
		style.BorderWidth = 1.0f;
		style.BorderColor = Rgba(0x3FE0FF40);
		return style;
	}

	void RegisterPreviews(Raven::SystemTable& systems)
	{
		systems.Preview({ "Arena HUD", [](Ui& ui) { DrawHud(ui, { .Score = 42, .Best = 57, .Drones = 6, .Time = 83.0f }); } });
		systems.Preview({ "Arena game over", [](Ui& ui)
		{
			DrawHud(ui, { .Score = 61, .Best = 48, .Drones = 9, .Time = 140.0f, .Over = true, .NewBest = true });
		} });
		systems.Preview({ "Arena fell out", [](Ui& ui)
		{
			DrawHud(ui, { .Score = 23, .Best = 48, .Drones = 5, .Time = 51.0f, .Over = true, .FellOut = true });
		} });
		systems.Preview({ "Pause menu", [](Ui& ui) { DrawPauseMenu(ui, false); } });
		systems.Preview({ "Key bindings", [](Ui& ui)
		{
			const std::vector<BindingRow> rows{ { "Forward", { "W", "Up" } }, { "Back", { "S", "Down" } },
				{ "Left", { "A", "Left" } }, { "Right", { "D", "Right" } }, { "Jump", { "Space" } },
				{ "Turn left", { "Q" } }, { "Turn right", { "E" } }, { "Pause", { "Esc", "P" } } };
			DrawBindings(ui, rows, 2, false);
		} });
		// Every script the engine's fonts cover, for checking shaping, fallback and direction at a glance.
		systems.Preview({ "Text samples", [](Ui& ui)
		{
			Style card;
			card.Absolute = true;
			card.Start = 40.0f;
			card.Top = 40.0f;
			card.Width = 620.0f;
			card.Padding = All(20.0f);
			card.Gap = 10.0f;
			card.Background = Rgba(0x0B1118E0);
			card.CornerRadius = 10.0f;
			ui.Begin("Samples", card);
			Style line;
			line.FontSize = 22.0f;
			ui.Text("Latin", "Raven engine: café, naïve, Œuvre, Ångström", line);
			ui.Text("Greek", "Ελληνικά: γρήγορη καφέ αλεπού", line);
			ui.Text("Cyrillic", "Кириллица: быстрая лиса", line);
			ui.Text("Korean", "한국어: 레이븐 엔진에 오신 것을 환영합니다", line);
			ui.Text("Arabic", "العربية: مرحبا بكم في محرك رافين", line);
			ui.Text("Mixed", "Score 42 نقطة in the arena", line);
			Style wrapped = line;
			wrapped.FontSize = 18.0f;
			ui.Text("Wrapped", "긴 한국어 문장은 단어 사이에서 줄이 바뀌어야 합니다. Long English text wraps between words "
				"as well, and Arabic does too: هذا نص عربي طويل يلتف بين الكلمات.", wrapped);
			ui.End();
		} });
	}
}

#pragma once

// The arena's screens (docs/GameServicesDesign.md, C9 second part) on the engine UI (docs/UIDesign.md).
// Each screen is drawn from a plain model, so the game systems and the UI preview entries (F6) draw
// the same code: systems fill the model from the world, previews from sample data.
//
// One file per screen holds its system and its drawing: Hud.cpp, PauseMenu.cpp and Bindings.cpp.
// This header and ArenaUi.cpp hold what they share: the models, the look and the preview entries.

#include "Raven/Core/GameSystems.hpp"
#include "Raven/UI/Ui.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Arena
{
	struct HudModel
	{
		std::uint32_t Score = 0;
		std::uint32_t Best = 0;
		std::uint32_t Drones = 0;
		float Time = 0.0f;
		bool Starting = false;
		bool Over = false;
		bool NewBest = false;
		// How the run ended: out of the arena, or caught by a drone.
		bool FellOut = false;
	};

	// One row of the key-binding screen: an action direction and its keys.
	struct BindingRow
	{
		std::string Label;
		std::vector<std::string> Keys;
	};

	enum class MenuChoice : std::uint8_t { None, Resume, OpenBindings, Back, ResetBindings, Rebind };

	struct MenuResult
	{
		MenuChoice Choice = MenuChoice::None;
		// For Rebind: the row and the binding slot within the row (Keys.size() adds one).
		std::size_t Row = 0;
		std::size_t Slot = 0;
	};

	// The arena's look: dark translucent panels, a cyan accent like the energy cells.
	inline constexpr Raven::UI::Color PanelColor = Raven::UI::Rgba(0x0B1118D0);
	inline constexpr Raven::UI::Color AccentColor = Raven::UI::Rgba(0x3FE0FFFF);
	inline constexpr Raven::UI::Color MutedColor = Raven::UI::Rgba(0xA8B4C0FF);
	inline constexpr Raven::UI::Color ButtonColor = Raven::UI::Rgba(0x1E2A36FF);
	Raven::UI::Style TextStyle(float size, Raven::UI::Color color = { 1, 1, 1, 1 });
	Raven::UI::Style ButtonStyle(float width = Raven::UI::Auto);
	// Fills the screen and centers its child.
	Raven::UI::Style CenteredPanel();
	Raven::UI::Style Card(float width);

	void DrawHud(Raven::UI::Ui& ui, const HudModel& model);
	MenuResult DrawPauseMenu(Raven::UI::Ui& ui, bool focus);
	MenuResult DrawBindings(Raven::UI::Ui& ui, const std::vector<BindingRow>& rows, std::int32_t listening, bool focus);

	// The UI preview entries (F6): each screen alone with sample data, and every script the fonts cover
	// (docs/UIDesign.md, layer 1).
	void RegisterPreviews(Raven::SystemTable& systems);
}

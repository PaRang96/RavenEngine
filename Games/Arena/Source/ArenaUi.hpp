#pragma once

// The arena's screens (docs/GameServicesDesign.md, C9 second part) on the engine UI (docs/UIDesign.md).
// Each screen is drawn from a plain model, so the game systems and the UI preview entries (F6) draw
// the same code: systems fill the model from the world, previews from sample data.

#include "Raven/ECS/Component.hpp"
#include "Raven/UI/Ui.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Arena
{
	enum class MenuScreen : std::uint8_t { Main, Bindings };

	// The pause menu's local view state on the camera rig entity; never simulation state (FD-47).
	struct ArenaMenuComponent : Raven::ECS::Component
	{
		MenuScreen Screen = MenuScreen::Main;
		// The binding row waiting for a key, or -1, and the binding within it.
		std::int32_t Listening = -1;
		// True on the frame a screen opens, so its first button takes keyboard focus.
		bool FocusPending = true;
		std::uint32_t Slot = 0;
		RAVEN_COMPONENT(Arena::ArenaMenuComponent, Screen, Listening, FocusPending, Slot);
	};

	struct HudModel
	{
		std::uint32_t Score = 0;
		std::uint32_t Best = 0;
		std::uint32_t Drones = 0;
		float Time = 0.0f;
		bool Starting = false;
		bool Over = false;
		bool NewBest = false;
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

	void DrawHud(Raven::UI::Ui& ui, const HudModel& model);
	MenuResult DrawPauseMenu(Raven::UI::Ui& ui, bool focus);
	MenuResult DrawBindings(Raven::UI::Ui& ui, const std::vector<BindingRow>& rows, std::int32_t listening, bool focus);
}

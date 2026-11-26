// The key-binding screen: each action direction with its keys from the live bindings; a click
// listens for a new key, and Reset restores the defaults.

#include "ArenaSystems.hpp"
#include "ArenaUi.hpp"

#include "Raven/Platform/Input.hpp"

#include <format>

namespace Arena
{
	namespace
	{
		struct BindingTarget
		{
			std::string_view Action;
			float Sign = 1.0f;
			const char* Label = "";
		};
		constexpr BindingTarget BindingTargets[]{
			{ MoveYAction, 1.0f, "Forward" }, { MoveYAction, -1.0f, "Back" }, { MoveXAction, -1.0f, "Left" },
			{ MoveXAction, 1.0f, "Right" }, { JumpAction, 1.0f, "Jump" }, { TurnAction, 1.0f, "Turn left" },
			{ TurnAction, -1.0f, "Turn right" }, { PauseAction, 1.0f, "Pause" } };

		// The indices, within one action's bindings, of a row's direction, in order.
		template<typename Bindings>
		std::vector<std::size_t> RowSlots(const Bindings& bindings, const BindingTarget& target)
		{
			std::vector<std::size_t> slots;
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
				for (const auto slot : RowSlots(bindings, target))
					row.Keys.push_back(actions.GetDisplayName({ bindings[slot].Code, bindings[slot].Modifiers }));
				rows.push_back(std::move(row));
			}
			return rows;
		}
	}

	void BindingsScreen(SystemContext& context, ArenaMenuComponent& menu, bool focus)
	{
		auto& actions = context.Actions;
		// A key captured for the row being rebound replaces its binding, or adds one.
		if (menu.Listening >= 0 && !actions.IsListening())
		{
			if (const auto chord = actions.TakeListened())
			{
				const auto& target = BindingTargets[std::min<std::size_t>(static_cast<std::size_t>(menu.Listening),
					std::size(BindingTargets) - 1)];
				const auto bindings = actions.GetBindings(PlayContext, target.Action);
				const auto slots = RowSlots(bindings, target);
				const auto slot = menu.Slot < slots.size() ? slots[menu.Slot] : bindings.size();
				if (chord->Code != InputCode::FromKey(Key::Escape))
					actions.Rebind(PlayContext, target.Action, slot, { chord->Code, chord->Modifiers, target.Sign });
			}
			menu.Listening = -1;
		}
		const auto result = DrawBindings(context.Ui, BindingRows(actions), menu.Listening, focus);
		if (result.Choice != MenuChoice::None)
			PlayUiSound(context, false);
		if (result.Choice == MenuChoice::Back)
			menu = OpenedScreen(MenuScreen::Main);
		else if (result.Choice == MenuChoice::ResetBindings)
			actions.ResetBindings(PlayContext);
		else if (result.Choice == MenuChoice::Rebind)
		{
			menu.Listening = static_cast<std::int32_t>(result.Row);
			menu.Slot = static_cast<std::uint32_t>(result.Slot);
			actions.BeginListening();
		}
	}

	MenuResult DrawBindings(Raven::UI::Ui& ui, const std::vector<BindingRow>& rows, std::int32_t listening, bool focus)
	{
		using namespace Raven::UI;
		MenuResult result;
		ui.Begin("Bindings", CenteredPanel());
		ui.Begin("Card", Card(560.0f));
		ui.Text("Title", "Key bindings", TextStyle(28.0f));
		ui.Text("Hint", listening >= 0 ? "Press a key for the highlighted action (Esc keeps it)" :
			"Click a key to change it, or + to add one", TextStyle(14.0f, MutedColor));
		for (std::size_t row = 0; row < rows.size(); ++row)
		{
			Style line;
			line.Direction = Flow::Row;
			line.Align = Place::Center;
			line.Gap = 8.0f;
			ui.BeginKey("Row", row + 1, line);
			Style label = TextStyle(16.0f, static_cast<std::int32_t>(row) == listening ? AccentColor : Raven::UI::Color{});
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

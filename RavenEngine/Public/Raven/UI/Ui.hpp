#pragma once

#include "Raven/Assets/AssetRef.hpp"

#include <cstdint>
#include <limits>
#include <string_view>

namespace Raven::UI
{
	// The engine's immediate UI over an engine-owned, ID-keyed cache (docs/UIDesign.md, decision (c)).
	// Game code declares elements every frame in the Control or Update phase; the engine lays them out
	// with Yoga after Update, draws them on the shared 2D path, and returns interactions computed from
	// the previous frame's layout. Nothing the UI keeps points into the game module.
	//
	// IDs: an element's ID is a stable key, hashed with the IDs of the Begin scopes around it. It is
	// never the displayed text, so switching languages keeps focus and state (UIDesign.md section 2).
	// Lists pass a per-item key, such as an EntityRef's value, through BeginKey.
	//
	// Sizes are logical pixels, the unit design tools use; the engine multiplies them by the UI scale
	// (the window's DPI scale times the user's setting) when drawing.

	// sRGB colors with straight alpha, as design tools give them.
	struct Color
	{
		float R = 1.0f;
		float G = 1.0f;
		float B = 1.0f;
		float A = 1.0f;
	};

	constexpr Color Rgba(std::uint32_t rgba) noexcept
	{
		return { static_cast<float>((rgba >> 24) & 0xFF) / 255.0f, static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
			static_cast<float>((rgba >> 8) & 0xFF) / 255.0f, static_cast<float>(rgba & 0xFF) / 255.0f };
	}

	inline constexpr float Auto = std::numeric_limits<float>::quiet_NaN();

	enum class Flow : std::uint8_t { Column, Row };
	enum class Place : std::uint8_t { Start, Center, End, Stretch, SpaceBetween };
	enum class TextAlignment : std::uint8_t { Start, Center, End };

	struct Edges
	{
		float Left = 0.0f;
		float Top = 0.0f;
		float Right = 0.0f;
		float Bottom = 0.0f;
	};

	constexpr Edges All(float value) noexcept { return { value, value, value, value }; }
	constexpr Edges Symmetric(float horizontal, float vertical) noexcept { return { horizontal, vertical, horizontal, vertical }; }

	// Layout and look of one element. Flex layout follows CSS (Yoga): Start and End follow the UI's
	// direction, so right-to-left screens mirror without code changes.
	struct Style
	{
		Flow Direction = Flow::Column;
		Place Justify = Place::Start;
		Place Align = Place::Stretch;
		float Gap = 0.0f;
		Edges Padding;
		Edges Margin;
		float Width = Auto;
		float Height = Auto;
		float MinWidth = Auto;
		float MinHeight = Auto;
		float MaxWidth = Auto;
		float MaxHeight = Auto;
		float Grow = 0.0f;
		float Shrink = 1.0f;
		// Absolute elements are placed by their offsets from the parent's edges, like Unreal's canvas
		// panel anchors; Start and End offsets follow the direction.
		bool Absolute = false;
		float Start = Auto;
		float Top = Auto;
		float End = Auto;
		float Bottom = Auto;
		Color Background{ 0.0f, 0.0f, 0.0f, 0.0f };
		Color BorderColor{ 0.0f, 0.0f, 0.0f, 0.0f };
		float BorderWidth = 0.0f;
		float CornerRadius = 0.0f;
		// Text: the em size in logical pixels, as design tools state it.
		float FontSize = 16.0f;
		Color TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		TextAlignment TextAlign = TextAlignment::Start;
		// A multiple of FontSize; zero uses the font's own spacing.
		float LineHeight = 0.0f;
		bool Wrap = true;
	};

	// What happened to an element, from the previous frame's layout.
	struct Interaction
	{
		bool Hovered = false;
		// Held down by the pointer or the keyboard.
		bool Pressed = false;
		// Activated this frame: pointer released over it, or Enter or Space while it has focus.
		bool Clicked = false;
		bool Focused = false;
	};

	class Ui
	{
	public:
		// A container scope. Child IDs are hashed under it.
		virtual void Begin(std::string_view id, const Style& style) = 0;
		// A container keyed by a value, for list items that move (pass an entity's value, an index).
		virtual void BeginKey(std::string_view id, std::uint64_t key, const Style& style) = 0;
		virtual void End() = 0;
		// Text is a value: it is shown, never hashed into the ID.
		virtual void Text(std::string_view id, std::string_view text, const Style& style) = 0;
		virtual Interaction Button(std::string_view id, std::string_view label, const Style& style) = 0;
		// Flips value when clicked.
		virtual Interaction Toggle(std::string_view id, std::string_view label, bool& value, const Style& style) = 0;
		virtual void Image(std::string_view id, AssetRef<TextureAsset> texture, const Style& style) = 0;
		// Gives keyboard focus to an element declared in this scope, for menus that open focused.
		virtual void Focus(std::string_view id) = 0;
		// True while some element has keyboard focus or the pointer is over an interactive element;
		// that input is withheld from gameplay.
		virtual bool IsCapturingInput() const noexcept = 0;
		virtual bool IsRightToLeft() const noexcept = 0;

	protected:
		~Ui() = default;
	};
}

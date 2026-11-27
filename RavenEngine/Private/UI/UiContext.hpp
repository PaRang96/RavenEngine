#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/Overlay.hpp"
#include "Raven/UI/Ui.hpp"
#include "Text/TextSystem.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Raven
{
	class AssetId;
}

namespace Raven::UI
{
	// One UI context: the game HUD today, later each editor panel (docs/UIDesign.md, section 2). It keeps
	// the frame's element list, the ID-keyed state (rectangles, hover, press, focus) and lays elements
	// out with Yoga after Update, then turns them into overlay quads. Host-owned: it survives module
	// reloads, and declaring the same UI after a reload finds the same state.
	class UiContext final : public Ui
	{
	public:
		// Resolves an image's texture to its bindless element; nullopt draws nothing.
		using TextureResolver = std::function<std::optional<std::uint32_t>(const AssetId&)>;

		explicit UiContext(Text::TextSystem& text);
		~UiContext();
		UiContext(const UiContext&) = delete;
		UiContext& operator=(const UiContext&) = delete;

		void SetTextureResolver(TextureResolver resolver) { m_ResolveTexture = std::move(resolver); }
		// Preview switches (UIDesign.md, layer 1): mirrored layout and pseudo-localized text.
		void SetRightToLeft(bool rightToLeft) noexcept { m_RightToLeft = rightToLeft; }
		void SetPseudoLocalized(bool pseudo) noexcept { m_Pseudo = pseudo; }
		bool IsPseudoLocalized() const noexcept { return m_Pseudo; }

		// Before routing: withholds pointer buttons over an interactive element of the previous frame's
		// layout, and Tab, Enter, Space and the arrows while an element has focus, so gameplay never
		// sees input the UI used (the UI's place above gameplay in FD-29's context stack).
		void FilterInput(InputState& input) const;
		// Starts a frame with the unfiltered input. Width and height are the area in framebuffer
		// pixels; scale is the UI scale (DPI scale times the user's setting).
		void BeginFrame(const InputState& raw, float width, float height, float scale);
		// Lays out what was declared and appends its quads. Collects state not declared for two frames.
		// Exported for hosts and tests that compile the session themselves.
		RAVEN_API void EndFrame(std::vector<OverlayQuad>& out);

		std::size_t GetElementCount() const noexcept { return m_Elements.size(); }

		// Ui
		void Begin(std::string_view id, const Style& style) override;
		void BeginKey(std::string_view id, std::uint64_t key, const Style& style) override;
		void End() override;
		void Text(std::string_view id, std::string_view text, const Style& style) override;
		Interaction Button(std::string_view id, std::string_view label, const Style& style) override;
		Interaction Toggle(std::string_view id, std::string_view label, bool& value, const Style& style) override;
		void Image(std::string_view id, AssetRef<TextureAsset> texture, const Style& style) override;
		void Focus(std::string_view id) override;
		using Ui::Text;
		using Ui::Button;
		using Ui::Toggle;
		std::string Localize(const UiText& text) const override;
		bool IsCapturingInput() const noexcept override;
		bool IsRightToLeft() const noexcept override { return m_RightToLeft; }

	private:
		enum class ElementKind : std::uint8_t { Container, Text, Button, Toggle, Image };

		struct Element
		{
			std::uint64_t Id = 0;
			std::uint32_t Parent = 0xFFFFFFFFu;
			ElementKind Kind = ElementKind::Container;
			Style Look;
			std::string Content;
			std::uint32_t Texture = 0;
			bool HasTexture = false;
			bool On = false;
			Interaction State;
			// Laid out this frame, in logical pixels from the root's top-left.
			float X = 0.0f, Y = 0.0f, Width = 0.0f, Height = 0.0f;
		};

		struct Entry
		{
			float X = 0.0f, Y = 0.0f, Width = 0.0f, Height = 0.0f;
			std::uint64_t LastFrame = 0;
			bool Interactive = false;
		};

		std::uint64_t MakeId(std::string_view id, std::uint64_t key = 0) const noexcept;
		std::uint32_t Add(std::uint64_t id, ElementKind kind, const Style& style, std::string_view content);
		Interaction Interact(std::uint64_t id);
		std::string Shown(std::string_view text) const;
		Text::TextStyle TextStyleOf(const Element& element, float wrapWidth) const;
		void Layout();
		void Draw(std::vector<OverlayQuad>& out);

		Text::TextSystem& m_Text;
		TextureResolver m_ResolveTexture;
		std::vector<Element> m_Elements;
		std::vector<std::uint32_t> m_Scopes;
		std::unordered_map<std::uint64_t, Entry> m_Cache;
		// Interactive IDs in the previous frame's declaration order, for keyboard navigation.
		std::vector<std::uint64_t> m_Order;
		std::vector<std::uint64_t> m_NextOrder;
		std::uint64_t m_Frame = 0;
		std::uint64_t m_Hovered = 0;
		std::uint64_t m_Active = 0;
		std::uint64_t m_Focused = 0;
		std::uint64_t m_Activated = 0;
		std::uint64_t m_FocusRequest = 0;
		bool m_PointerReleased = false;
		bool m_PointerPressed = false;
		float m_Width = 0.0f, m_Height = 0.0f, m_Scale = 1.0f;
		float m_CursorX = -1.0f, m_CursorY = -1.0f;
		bool m_RightToLeft = false;
		bool m_Pseudo = false;
	};
}

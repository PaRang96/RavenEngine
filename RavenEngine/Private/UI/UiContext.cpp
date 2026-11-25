#include "UI/UiContext.hpp"

#include "Raven/Assets/AssetRecord.hpp"

#include <yoga/Yoga.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Raven::UI
{
	namespace
	{
		constexpr std::uint32_t NoParent = 0xFFFFFFFFu;
		// Declared state not seen for this many frames is collected (UIDesign.md section 2).
		constexpr std::uint64_t KeepFrames = 2;
		// The toggle's box: a square of this many font sizes, then a gap.
		constexpr float ToggleBox = 1.0f;
		constexpr float ToggleGap = 0.5f;

		std::uint64_t Mix(std::uint64_t hash, std::string_view text) noexcept
		{
			for (const char character : text)
			{
				hash ^= static_cast<unsigned char>(character);
				hash *= 1099511628211ull;
			}
			return hash;
		}

		float ToLinear(float value) noexcept
		{
			value = std::clamp(value, 0.0f, 1.0f);
			return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

		std::array<float, 4> Linear(const Color& color) noexcept
		{
			return { ToLinear(color.R), ToLinear(color.G), ToLinear(color.B), std::clamp(color.A, 0.0f, 1.0f) };
		}

		Color Shade(const Color& color, float amount) noexcept
		{
			// Toward white for positive amounts, toward black for negative ones.
			const float target = amount > 0.0f ? 1.0f : 0.0f;
			const float weight = std::abs(amount);
			return { color.R + (target - color.R) * weight, color.G + (target - color.G) * weight,
				color.B + (target - color.B) * weight, color.A };
		}

		bool Set(float value) noexcept
		{
			return !std::isnan(value);
		}

		YGJustify JustifyOf(Place place) noexcept
		{
			switch (place)
			{
			case Place::Center: return YGJustifyCenter;
			case Place::End: return YGJustifyFlexEnd;
			case Place::SpaceBetween: return YGJustifySpaceBetween;
			default: return YGJustifyFlexStart;
			}
		}

		YGAlign AlignOf(Place place) noexcept
		{
			switch (place)
			{
			case Place::Start: return YGAlignFlexStart;
			case Place::Center: return YGAlignCenter;
			case Place::End: return YGAlignFlexEnd;
			case Place::SpaceBetween: return YGAlignSpaceBetween;
			default: return YGAlignStretch;
			}
		}

		void Apply(YGNodeRef node, const Style& style)
		{
			YGNodeStyleSetFlexDirection(node, style.Direction == Flow::Row ? YGFlexDirectionRow : YGFlexDirectionColumn);
			YGNodeStyleSetJustifyContent(node, JustifyOf(style.Justify));
			YGNodeStyleSetAlignItems(node, AlignOf(style.Align));
			YGNodeStyleSetGap(node, YGGutterAll, style.Gap);
			YGNodeStyleSetPadding(node, YGEdgeStart, style.Padding.Left);
			YGNodeStyleSetPadding(node, YGEdgeTop, style.Padding.Top);
			YGNodeStyleSetPadding(node, YGEdgeEnd, style.Padding.Right);
			YGNodeStyleSetPadding(node, YGEdgeBottom, style.Padding.Bottom);
			YGNodeStyleSetMargin(node, YGEdgeStart, style.Margin.Left);
			YGNodeStyleSetMargin(node, YGEdgeTop, style.Margin.Top);
			YGNodeStyleSetMargin(node, YGEdgeEnd, style.Margin.Right);
			YGNodeStyleSetMargin(node, YGEdgeBottom, style.Margin.Bottom);
			YGNodeStyleSetBorder(node, YGEdgeAll, style.BorderWidth);
			if (Set(style.Width))
				YGNodeStyleSetWidth(node, style.Width);
			if (Set(style.Height))
				YGNodeStyleSetHeight(node, style.Height);
			if (Set(style.MinWidth))
				YGNodeStyleSetMinWidth(node, style.MinWidth);
			if (Set(style.MinHeight))
				YGNodeStyleSetMinHeight(node, style.MinHeight);
			if (Set(style.MaxWidth))
				YGNodeStyleSetMaxWidth(node, style.MaxWidth);
			if (Set(style.MaxHeight))
				YGNodeStyleSetMaxHeight(node, style.MaxHeight);
			YGNodeStyleSetFlexGrow(node, style.Grow);
			YGNodeStyleSetFlexShrink(node, style.Shrink);
			if (style.Absolute)
			{
				YGNodeStyleSetPositionType(node, YGPositionTypeAbsolute);
				if (Set(style.Start))
					YGNodeStyleSetPosition(node, YGEdgeStart, style.Start);
				if (Set(style.Top))
					YGNodeStyleSetPosition(node, YGEdgeTop, style.Top);
				if (Set(style.End))
					YGNodeStyleSetPosition(node, YGEdgeEnd, style.End);
				if (Set(style.Bottom))
					YGNodeStyleSetPosition(node, YGEdgeBottom, style.Bottom);
			}
		}

		struct MeasureContext
		{
			Text::TextSystem* Text = nullptr;
			const std::string* Content = nullptr;
			Text::TextStyle Style;
			float Scale = 1.0f;
			float Extra = 0.0f;
		};

		YGSize MeasureText(YGNodeConstRef node, float width, YGMeasureMode widthMode, float, YGMeasureMode)
		{
			const auto& context = *static_cast<const MeasureContext*>(YGNodeGetContext(node));
			auto style = context.Style;
			if (widthMode != YGMeasureModeUndefined && style.WrapWidth > 0.0f)
				style.WrapWidth = std::max(1.0f, (width - context.Extra) * context.Scale);
			else
				style.WrapWidth = 0.0f;
			const auto& block = context.Text->Layout(*context.Content, style);
			float measured = block.Width / context.Scale + context.Extra;
			if (widthMode == YGMeasureModeAtMost)
				measured = std::min(measured, width);
			const float height = std::max(block.Height / context.Scale, context.Style.Size / context.Scale * 1.2f);
			return { std::ceil(measured), std::ceil(height) };
		}
	}

	UiContext::UiContext(Text::TextSystem& text)
		: m_Text(text)
	{
	}

	UiContext::~UiContext() = default;

	std::uint64_t UiContext::MakeId(std::string_view id, std::uint64_t key) const noexcept
	{
		std::uint64_t hash = m_Scopes.empty() ? 14695981039346656037ull : m_Elements[m_Scopes.back()].Id;
		hash = Mix(hash ^ 0x9E3779B97F4A7C15ull, id);
		if (key != 0)
			hash = (hash ^ key) * 1099511628211ull;
		return hash == 0 ? 1 : hash;
	}

	std::uint32_t UiContext::Add(std::uint64_t id, ElementKind kind, const Style& style, std::string_view content)
	{
		Element element;
		element.Id = id;
		element.Parent = m_Scopes.empty() ? NoParent : m_Scopes.back();
		element.Kind = kind;
		element.Look = style;
		element.Content = std::string(content);
		m_Elements.push_back(std::move(element));
		return static_cast<std::uint32_t>(m_Elements.size() - 1);
	}

	Interaction UiContext::Interact(std::uint64_t id)
	{
		m_NextOrder.push_back(id);
		Interaction result;
		result.Hovered = m_Hovered == id;
		result.Pressed = m_Active == id;
		result.Clicked = m_Activated == id;
		result.Focused = m_Focused == id;
		return result;
	}

	std::string UiContext::Shown(std::string_view text) const
	{
		if (!m_Pseudo)
			return std::string(text);
		// Pseudo-localization (UIDesign.md, layer 1): accented letters and about 30% more length, in
		// brackets, so clipped, concatenated or untranslated text shows at once.
		static constexpr std::array<const char*, 26> lower{ "\xC3\xA1", "b", "\xC3\xA7", "d", "\xC3\xA9", "f", "g", "h",
			"\xC3\xAD", "j", "k", "\xC4\xBA", "m", "\xC3\xB1", "\xC3\xB3", "p", "q", "\xC5\x95", "\xC5\xA1", "t",
			"\xC3\xBA", "v", "\xC5\xB5", "x", "\xC3\xBD", "\xC5\xBE" };
		std::string result = "[";
		for (const char character : text)
		{
			if (character >= 'a' && character <= 'z')
				result += lower[static_cast<std::size_t>(character - 'a')];
			else if (character >= 'A' && character <= 'Z')
			{
				result += character;
				result += "\xCC\x81";
			}
			else
				result += character;
		}
		result.append(std::max<std::size_t>(1, text.size() * 3 / 10), '~');
		result += "]";
		return result;
	}

	Text::TextStyle UiContext::TextStyleOf(const Element& element, float wrapWidth) const
	{
		Text::TextStyle style;
		style.Size = element.Look.FontSize * m_Scale;
		style.LineHeight = element.Look.LineHeight;
		style.WrapWidth = element.Look.Wrap ? wrapWidth : 0.0f;
		style.Direction = m_RightToLeft ? Text::TextDirection::RightToLeft : Text::TextDirection::LeftToRight;
		style.Align = element.Look.TextAlign == TextAlignment::Center ? Text::TextAlignment::Center :
			element.Look.TextAlign == TextAlignment::End ? Text::TextAlignment::End : Text::TextAlignment::Start;
		return style;
	}

	void UiContext::FilterInput(InputState& input) const
	{
		const auto strip = [&input](InputCode code)
		{
			std::erase_if(input.Events, [code](const InputEvent& event)
			{
				return event.Code.Device == code.Device && event.Code.Value == code.Value &&
					event.Transition != InputTransition::Released;
			});
			if (code.Device == InputDevice::Mouse)
				input.MouseButtons[code.Value].Pressed = input.MouseButtons[code.Value].Down = false;
			else
				input.Keys[code.Value].Pressed = input.Keys[code.Value].Down = false;
		};
		if (input.HasCursorPosition && m_Scale > 0.0f)
		{
			const float x = static_cast<float>(input.CursorX) / m_Scale;
			const float y = static_cast<float>(input.CursorY) / m_Scale;
			for (const auto id : m_Order)
			{
				const auto found = m_Cache.find(id);
				if (found != m_Cache.end() && x >= found->second.X && y >= found->second.Y &&
					x < found->second.X + found->second.Width && y < found->second.Y + found->second.Height)
				{
					for (std::uint16_t button = 0; button < static_cast<std::uint16_t>(MouseButton::Count); ++button)
						strip({ InputDevice::Mouse, button });
					break;
				}
			}
		}
		if (m_Focused != 0)
			for (const Key key : { Key::Tab, Key::Enter, Key::Space, Key::NumpadEnter, Key::Up, Key::Down, Key::Left, Key::Right })
				strip(InputCode::FromKey(key));
	}

	void UiContext::BeginFrame(const InputState& raw, float width, float height, float scale)
	{
		++m_Frame;
		m_Elements.clear();
		m_Scopes.clear();
		m_Order.swap(m_NextOrder);
		m_NextOrder.clear();
		m_Scale = scale > 0.0f ? scale : 1.0f;
		m_Width = width / m_Scale;
		m_Height = height / m_Scale;
		m_Activated = 0;

		// Hover: the last-declared interactive element under the pointer, from last frame's layout.
		m_Hovered = 0;
		if (raw.HasCursorPosition)
		{
			m_CursorX = static_cast<float>(raw.CursorX) / m_Scale;
			m_CursorY = static_cast<float>(raw.CursorY) / m_Scale;
			for (const auto id : m_Order)
			{
				const auto found = m_Cache.find(id);
				if (found != m_Cache.end() && m_CursorX >= found->second.X && m_CursorY >= found->second.Y &&
					m_CursorX < found->second.X + found->second.Width && m_CursorY < found->second.Y + found->second.Height)
					m_Hovered = id;
			}
		}
		const auto left = raw.GetMouseButtonState(MouseButton::Left);
		if (left.Pressed && m_Hovered != 0)
			m_Active = m_Hovered;
		if (left.Released)
		{
			if (m_Active != 0 && m_Active == m_Hovered)
				m_Activated = m_Active;
			m_Active = 0;
		}

		// Keyboard focus among last frame's interactive elements.
		if (m_Focused != 0 && std::find(m_Order.begin(), m_Order.end(), m_Focused) == m_Order.end())
			m_Focused = 0;
		const auto step = [this](int direction)
		{
			if (m_Order.empty())
				return;
			const auto current = std::find(m_Order.begin(), m_Order.end(), m_Focused);
			if (current == m_Order.end())
			{
				m_Focused = direction > 0 ? m_Order.front() : m_Order.back();
				return;
			}
			const auto index = static_cast<std::ptrdiff_t>(current - m_Order.begin());
			const auto count = static_cast<std::ptrdiff_t>(m_Order.size());
			m_Focused = m_Order[static_cast<std::size_t>((index + direction + count) % count)];
		};
		// From the events, in order, with the modifiers held at each one; held arrows repeat.
		for (const auto& event : raw.Events)
		{
			if (event.Code.Device != InputDevice::Keyboard || event.Transition == InputTransition::Released)
				continue;
			const auto key = event.Code.GetKey();
			const bool repeated = event.Transition == InputTransition::Repeated;
			if (key == Key::Tab)
				step((event.Modifiers & Modifier::Shift) != Modifier::None ? -1 : 1);
			else if (key == Key::Down)
				step(1);
			else if (key == Key::Up)
				step(-1);
			else if (!repeated && m_Focused != 0 && (key == Key::Enter || key == Key::Space || key == Key::NumpadEnter))
				m_Activated = m_Focused;
		}
	}

	void UiContext::Begin(std::string_view id, const Style& style)
	{
		m_Scopes.push_back(Add(MakeId(id), ElementKind::Container, style, {}));
	}

	void UiContext::BeginKey(std::string_view id, std::uint64_t key, const Style& style)
	{
		m_Scopes.push_back(Add(MakeId(id, key), ElementKind::Container, style, {}));
	}

	void UiContext::End()
	{
		if (!m_Scopes.empty())
			m_Scopes.pop_back();
	}

	void UiContext::Text(std::string_view id, std::string_view text, const Style& style)
	{
		Add(MakeId(id), ElementKind::Text, style, Shown(text));
	}

	Interaction UiContext::Button(std::string_view id, std::string_view label, const Style& style)
	{
		const auto key = MakeId(id);
		const auto index = Add(key, ElementKind::Button, style, Shown(label));
		m_Elements[index].State = Interact(key);
		return m_Elements[index].State;
	}

	Interaction UiContext::Toggle(std::string_view id, std::string_view label, bool& value, const Style& style)
	{
		const auto key = MakeId(id);
		const auto index = Add(key, ElementKind::Toggle, style, Shown(label));
		auto result = Interact(key);
		if (result.Clicked)
			value = !value;
		m_Elements[index].State = result;
		m_Elements[index].On = value;
		return result;
	}

	void UiContext::Image(std::string_view id, AssetRef<TextureAsset> texture, const Style& style)
	{
		const auto index = Add(MakeId(id), ElementKind::Image, style, {});
		if (texture.IsValid() && m_ResolveTexture)
			if (const auto element = m_ResolveTexture(texture.Id))
			{
				m_Elements[index].Texture = *element;
				m_Elements[index].HasTexture = true;
			}
	}

	void UiContext::Focus(std::string_view id)
	{
		m_FocusRequest = MakeId(id);
	}

	bool UiContext::IsCapturingInput() const noexcept
	{
		return m_Focused != 0 || m_Hovered != 0 || m_Active != 0;
	}

	void UiContext::Layout()
	{
		if (m_Elements.empty())
			return;
		// One Yoga tree per frame from the element list (UIDesign.md section 3). A HUD or menu has tens
		// of elements; per-ID retained nodes with dirty flags arrive with editor-sized trees.
		YGConfigRef config = YGConfigNew();
		YGConfigSetPointScaleFactor(config, m_Scale);
		std::vector<YGNodeRef> nodes(m_Elements.size());
		std::vector<MeasureContext> measures(m_Elements.size());
		YGNodeRef root = YGNodeNewWithConfig(config);
		YGNodeStyleSetWidth(root, m_Width);
		YGNodeStyleSetHeight(root, m_Height);
		for (std::size_t index = 0; index < m_Elements.size(); ++index)
		{
			auto& element = m_Elements[index];
			YGNodeRef node = YGNodeNewWithConfig(config);
			nodes[index] = node;
			Apply(node, element.Look);
			if (element.Kind == ElementKind::Text || element.Kind == ElementKind::Button || element.Kind == ElementKind::Toggle)
			{
				auto& measure = measures[index];
				measure.Text = &m_Text;
				measure.Content = &element.Content;
				measure.Style = TextStyleOf(element, element.Look.Wrap ? 1.0f : 0.0f);
				measure.Scale = m_Scale;
				measure.Extra = element.Kind == ElementKind::Toggle ? element.Look.FontSize * (ToggleBox + ToggleGap) : 0.0f;
				YGNodeSetContext(node, &measure);
				YGNodeSetMeasureFunc(node, &MeasureText);
			}
			YGNodeRef parent = element.Parent == NoParent ? root : nodes[element.Parent];
			YGNodeInsertChild(parent, node, YGNodeGetChildCount(parent));
		}
		YGNodeCalculateLayout(root, m_Width, m_Height, m_RightToLeft ? YGDirectionRTL : YGDirectionLTR);
		for (std::size_t index = 0; index < m_Elements.size(); ++index)
		{
			auto& element = m_Elements[index];
			const float parentX = element.Parent == NoParent ? 0.0f : m_Elements[element.Parent].X;
			const float parentY = element.Parent == NoParent ? 0.0f : m_Elements[element.Parent].Y;
			element.X = parentX + YGNodeLayoutGetLeft(nodes[index]);
			element.Y = parentY + YGNodeLayoutGetTop(nodes[index]);
			element.Width = YGNodeLayoutGetWidth(nodes[index]);
			element.Height = YGNodeLayoutGetHeight(nodes[index]);
		}
		YGNodeFreeRecursive(root);
		YGConfigFree(config);
	}

	void UiContext::Draw(std::vector<OverlayQuad>& out)
	{
		const std::array<float, 4> clip{ -1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f };
		const auto box = [&](float x, float y, float width, float height, const Color& fill, const Color& border,
			float borderWidth, float radius)
		{
			if (fill.A <= 0.0f && (border.A <= 0.0f || borderWidth <= 0.0f))
				return;
			OverlayQuad quad;
			quad.Rect[0] = x * m_Scale;
			quad.Rect[1] = y * m_Scale;
			quad.Rect[2] = width * m_Scale;
			quad.Rect[3] = height * m_Scale;
			const auto linear = Linear(fill);
			std::copy(linear.begin(), linear.end(), quad.Color);
			const auto edge = Linear(border);
			std::copy(edge.begin(), edge.end(), quad.BorderColor);
			quad.BorderWidth = borderWidth * m_Scale;
			quad.CornerRadius = radius * m_Scale;
			out.push_back(quad);
		};
		for (const auto& element : m_Elements)
		{
			const auto& look = element.Look;
			Color fill = look.Background;
			Color border = look.BorderColor;
			float borderWidth = look.BorderWidth;
			if (element.Kind == ElementKind::Button || element.Kind == ElementKind::Toggle)
			{
				if (element.State.Pressed)
					fill = Shade(fill, -0.15f);
				else if (element.State.Hovered)
					fill = Shade(fill, 0.12f);
				if (element.State.Focused)
				{
					// The focus ring: the border color, or a light outline when the style has none.
					border = border.A > 0.0f ? border : Color{ 1.0f, 1.0f, 1.0f, 0.9f };
					borderWidth = std::max(borderWidth, 2.0f);
				}
			}
			box(element.X, element.Y, element.Width, element.Height, fill, border, borderWidth, look.CornerRadius);
			if (element.Kind == ElementKind::Image && element.HasTexture)
			{
				OverlayQuad quad;
				quad.Rect[0] = (element.X + look.Padding.Left) * m_Scale;
				quad.Rect[1] = (element.Y + look.Padding.Top) * m_Scale;
				quad.Rect[2] = std::max(0.0f, element.Width - look.Padding.Left - look.Padding.Right) * m_Scale;
				quad.Rect[3] = std::max(0.0f, element.Height - look.Padding.Top - look.Padding.Bottom) * m_Scale;
				quad.Uv[2] = quad.Uv[3] = 1.0f;
				const auto tint = Linear(look.TextColor);
				std::copy(tint.begin(), tint.end(), quad.Color);
				quad.Texture = element.Texture;
				quad.Flags = OverlayFlags::Image;
				quad.CornerRadius = look.CornerRadius * m_Scale;
				out.push_back(quad);
			}
			if (element.Content.empty() && element.Kind != ElementKind::Toggle)
				continue;
			if (element.Kind == ElementKind::Text || element.Kind == ElementKind::Button || element.Kind == ElementKind::Toggle)
			{
				const float inset = look.BorderWidth;
				float left = element.X + inset + (m_RightToLeft ? look.Padding.Right : look.Padding.Left);
				const float top = element.Y + inset + look.Padding.Top;
				float width = std::max(0.0f, element.Width - 2.0f * inset - look.Padding.Left - look.Padding.Right);
				const float height = std::max(0.0f, element.Height - 2.0f * inset - look.Padding.Top - look.Padding.Bottom);
				if (element.Kind == ElementKind::Toggle)
				{
					// The box at the start edge, then the label.
					const float side = look.FontSize * ToggleBox;
					const float boxX = m_RightToLeft ? left + width - side : left;
					const float boxY = top + (height - side) * 0.5f;
					box(boxX, boxY, side, side, element.On ? look.TextColor : Color{ 0, 0, 0, 0 }, look.TextColor,
						std::max(1.5f, side * 0.1f), side * 0.2f);
					const float used = look.FontSize * (ToggleBox + ToggleGap);
					width = std::max(0.0f, width - used);
					if (!m_RightToLeft)
						left += used;
				}
				const auto style = TextStyleOf(element, width * m_Scale);
				const auto& block = m_Text.Layout(element.Content, style);
				float x = left;
				if (!look.Wrap)
				{
					const float slack = width - block.Width / m_Scale;
					const bool end = (look.TextAlign == TextAlignment::End) != m_RightToLeft;
					if (look.TextAlign == TextAlignment::Center)
						x += slack * 0.5f;
					else if (end)
						x += slack;
				}
				// Buttons and toggles center their label vertically; text starts at the top.
				const float y = element.Kind == ElementKind::Text ? top : top + (height - block.Height / m_Scale) * 0.5f;
				m_Text.AppendQuads(block, std::round(x * m_Scale), std::round(y * m_Scale), Linear(look.TextColor), clip, out);
			}
		}
	}

	void UiContext::EndFrame(std::vector<OverlayQuad>& out)
	{
		Layout();
		for (const auto& element : m_Elements)
		{
			auto& entry = m_Cache[element.Id];
			entry = { element.X, element.Y, element.Width, element.Height, m_Frame,
				element.Kind == ElementKind::Button || element.Kind == ElementKind::Toggle };
		}
		if (m_FocusRequest != 0)
		{
			m_Focused = m_FocusRequest;
			m_FocusRequest = 0;
		}
		std::erase_if(m_Cache, [this](const auto& entry) { return m_Frame - entry.second.LastFrame > KeepFrames; });
		Draw(out);
	}
}

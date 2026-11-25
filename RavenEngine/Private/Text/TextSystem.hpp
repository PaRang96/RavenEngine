#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Renderer/Overlay.hpp"
#include "Raven/Renderer/TextureHandle.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Raven
{
	class JobSystem;
	class VulkanContext;
}

namespace Raven::Text
{
	// Paragraph direction, as the UI sets it (right-to-left for Arabic and Hebrew screens).
	enum class TextDirection : std::uint8_t { LeftToRight, RightToLeft };
	enum class TextAlignment : std::uint8_t { Start, Center, End };

	// What a piece of text is shaped and broken with (docs/UIDesign.md, "Sizes match the design tools").
	struct TextStyle
	{
		// The em size in framebuffer pixels: the designer's size in logical pixels times the UI scale.
		float Size = 16.0f;
		// A multiple of Size; zero uses the font's own line spacing (typo metrics when the font asks for
		// them, otherwise hhea).
		float LineHeight = 0.0f;
		// Zero or less keeps each paragraph on one line.
		float WrapWidth = 0.0f;
		TextDirection Direction = TextDirection::LeftToRight;
		TextAlignment Align = TextAlignment::Start;
	};

	// One glyph placed in a laid-out block, in pixels from the block's top-left corner.
	struct PlacedGlyph
	{
		std::uint16_t Font = 0;
		std::uint32_t Glyph = 0;
		float X = 0.0f; // pen position on the baseline
		float Baseline = 0.0f;
	};

	struct TextBlock
	{
		std::vector<PlacedGlyph> Glyphs;
		float Width = 0.0f;
		float Height = 0.0f;
		std::uint32_t Lines = 0;
		float Size = 16.0f;
	};

	// The engine's text stack (docs/UIDesign.md, section 4; decisions U2, U4 and U5): ICU for bidi,
	// script runs and line breaks; HarfBuzz for shaping with font fallback; msdfgen and msdf-atlas-gen
	// for MTSDF glyphs generated on demand on background workers into atlas pages, which reach the GPU
	// through the overlay's texture uploads. Host-owned, shared by every UI context.
	class TextSystem
	{
	public:
		// Loads ICU's data and the engine fonts from contentRoot (Fonts/, ICU/); throws when they are
		// missing or unreadable.
		TextSystem(VulkanContext& renderer, JobSystem& workers, const std::filesystem::path& contentRoot);
		~TextSystem();
		TextSystem(const TextSystem&) = delete;
		TextSystem& operator=(const TextSystem&) = delete;

		// Shapes every paragraph not yet cached, in parallel on the workers. Optional: Layout shapes
		// misses itself, one at a time.
		void Prepare(std::span<const std::string_view> texts, std::span<const TextStyle> styles);
		// Lays text out, from the cache when the same text and style were used recently. The block
		// stays valid until the next EndFrame.
		const TextBlock& Layout(std::string_view utf8, const TextStyle& style);
		// Appends one glyph quad per visible glyph at (x, y), the block's top-left corner. False, with
		// nothing appended, while some glyph is still being generated: text never draws half-formed.
		bool AppendQuads(const TextBlock& block, float x, float y, const std::array<float, 4>& color,
			const std::array<float, 4>& clip, std::vector<OverlayQuad>& out);

		// Once per frame before drawing: places finished glyphs into pages and lists their uploads.
		RAVEN_API std::span<const OverlayTextureUpload> TakeUploads();
		// After DrawFrame: true when the frame was presented, so the listed uploads reached the GPU and
		// their pixels can be dropped; otherwise they are listed again next frame.
		RAVEN_API void EndFrame(bool presented);

		struct Stats
		{
			std::size_t Fonts = 0;
			std::size_t Glyphs = 0;
			std::size_t PendingGlyphs = 0;
			std::size_t Pages = 0;
			std::size_t CachedTexts = 0;
		};
		Stats GetStats() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

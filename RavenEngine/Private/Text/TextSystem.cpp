#include "Text/TextSystem.hpp"

#include "Core/JobSystem.hpp"
#include "Core/PathText.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Renderer/TextureDesc.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <hb-ot.h>
#include <hb.h>

#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/udata.h>
#include <unicode/uscript.h>
#include <unicode/ustring.h>
#include <unicode/utf16.h>

#include <msdf-atlas-gen/AtlasGenerator.h>
#include <msdf-atlas-gen/GlyphGeometry.h>
#include <msdf-atlas-gen/glyph-generators.h>
#include <msdfgen-ext.h>
#include <msdfgen.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Raven::Text
{
	namespace
	{
		// Glyph pages: RGBA8 MTSDF, filled on demand with a simple shelf packer.
		constexpr std::uint32_t PageSize = 1024;
		// Atlas glyphs are generated at 40 pixels per em with a 4-pixel distance range: sharp from small
		// UI text to large titles, since the field scales.
		constexpr double AtlasEm = 40.0;
		constexpr double AtlasRange = 4.0;
		constexpr std::uint32_t GlyphGap = 1;
		// Cached blocks and shaped paragraphs not used for this many frames are dropped.
		constexpr std::uint64_t CacheFrames = 240;
		// The engine fonts, tried in this order for each character (Content/Fonts/README.md).
		constexpr const char* FontFiles[] = { "NotoSans.ttf", "NotoSansArabic.ttf", "NotoSansKR.ttf" };

		std::uint64_t Hash(std::string_view text, std::uint64_t seed) noexcept
		{
			std::uint64_t hash = 14695981039346656037ull ^ seed;
			for (const char character : text)
			{
				hash ^= static_cast<unsigned char>(character);
				hash *= 1099511628211ull;
			}
			return hash;
		}

		std::uint64_t Bits(float value) noexcept
		{
			return std::bit_cast<std::uint32_t>(value);
		}
	}

	struct Font
	{
		std::string Name;
		std::vector<std::byte> Bytes;
		hb_blob_t* Blob = nullptr;
		hb_face_t* Face = nullptr;
		hb_font_t* Shaper = nullptr;
		msdfgen::FontHandle* Outlines = nullptr;
		// FreeType faces are not thread-safe; only reading an outline holds it, not generation.
		std::mutex OutlineMutex;
		double UnitsPerEm = 1000.0;
		// In em; the descender is negative. HarfBuzz picks the typo metrics when the font sets
		// USE_TYPO_METRICS and hhea otherwise, the rule of docs/UIDesign.md.
		float Ascender = 0.8f;
		float Descender = -0.2f;
		float LineGap = 0.0f;

		~Font()
		{
			if (Outlines)
				msdfgen::destroyFont(Outlines);
			hb_font_destroy(Shaper);
			hb_face_destroy(Face);
			hb_blob_destroy(Blob);
		}
	};

	// A paragraph shaped once, in em units, independent of size and wrap width.
	struct ShapedGlyph
	{
		std::uint16_t Font = 0;
		std::uint32_t Glyph = 0;
		float Advance = 0.0f;
		float OffsetX = 0.0f;
		float OffsetY = 0.0f;
		std::uint32_t Cluster = 0;
	};

	struct ShapedRun
	{
		std::uint32_t First = 0;
		std::uint32_t Count = 0;
		std::uint32_t Start = 0;
		std::uint32_t End = 0;
		std::uint8_t Level = 0;
	};

	struct Shaped
	{
		std::u16string Text;
		std::vector<ShapedGlyph> Glyphs;
		std::vector<ShapedRun> Runs;
		// Per UTF-16 index: a line may break before it.
		std::vector<std::uint8_t> BreakBefore;
		// The advance of the glyphs whose cluster starts at each UTF-16 index, in em.
		std::vector<float> Advance;
		std::uint64_t LastUsed = 0;
	};

	struct CachedBlock
	{
		TextBlock Block;
		std::uint64_t LastUsed = 0;
	};

	struct GlyphEntry
	{
		enum class State : std::uint8_t { Pending, Ready, Empty, Failed } Status = State::Pending;
		std::uint32_t Page = 0;
		float Uv[4]{};
		// Plane bounds in em, y up from the baseline: left, bottom, right, top.
		float Plane[4]{};
	};

	struct Page
	{
		TextureHandle Texture;
		std::uint32_t ShelfY = 0;
		std::uint32_t ShelfHeight = 0;
		std::uint32_t CursorX = 0;
	};

	struct FinishedGlyph
	{
		std::uint64_t Key = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		// RGBA8, top row first.
		std::vector<std::byte> Pixels;
		float Plane[4]{};
		bool Empty = false;
		bool Failed = false;
	};

	struct PendingUpload
	{
		TextureHandle Texture;
		std::uint32_t X = 0;
		std::uint32_t Y = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<std::byte> Pixels;
	};

	struct TextSystem::Impl
	{
		Impl(VulkanContext& renderer, JobSystem& workers, const std::filesystem::path& root)
			: Renderer(renderer), Workers(workers)
		{
			// ICU's filtered data (ThirdParty/icu4c/README.md), handed over before any ICU call.
			const auto icu = root / "ICU" / "icudt79l.dat";
			IcuData = Files::ReadBinary(icu, 64 * 1024 * 1024);
			UErrorCode status = U_ZERO_ERROR;
			udata_setCommonData(IcuData.data(), &status);
			if (U_FAILURE(status))
				throw std::runtime_error(std::format("ICU data rejected ({}): {}", u_errorName(status), PathToUtf8(icu)));

			Freetype = msdfgen::initializeFreetype();
			if (!Freetype)
				throw std::runtime_error("FreeType did not start");
			for (const char* file : FontFiles)
			{
				auto font = std::make_unique<Font>();
				const auto path = root / "Fonts" / file;
				font->Name = file;
				font->Bytes = Files::ReadBinary(path, 64 * 1024 * 1024);
				font->Blob = hb_blob_create(reinterpret_cast<const char*>(font->Bytes.data()),
					static_cast<unsigned>(font->Bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
				font->Face = hb_face_create(font->Blob, 0);
				font->UnitsPerEm = hb_face_get_upem(font->Face);
				font->Shaper = hb_font_create(font->Face);
				hb_font_set_scale(font->Shaper, static_cast<int>(font->UnitsPerEm), static_cast<int>(font->UnitsPerEm));
				font->Outlines = msdfgen::loadFontData(Freetype, reinterpret_cast<const msdfgen::byte*>(font->Bytes.data()),
					static_cast<int>(font->Bytes.size()));
				if (!font->Outlines || hb_face_get_glyph_count(font->Face) == 0)
					throw std::runtime_error("Cannot read font " + PathToUtf8(path));
				// Variable fonts draw at regular weight (400), the weight designers start from; some ship a
				// lighter default instance. Shaping and outlines use the same instance, so advances match.
				std::array<hb_ot_var_axis_info_t, 8> axes{};
				unsigned axisCount = static_cast<unsigned>(axes.size());
				hb_ot_var_get_axis_infos(font->Face, 0, &axisCount, axes.data());
				for (unsigned axis = 0; axis < axisCount; ++axis)
					if (axes[axis].tag == HB_TAG('w', 'g', 'h', 't') && axes[axis].default_value != 400.0f &&
						axes[axis].min_value <= 400.0f && axes[axis].max_value >= 400.0f)
					{
						const hb_variation_t regular{ HB_TAG('w', 'g', 'h', 't'), 400.0f };
						hb_font_set_variations(font->Shaper, &regular, 1);
						msdfgen::setFontVariationAxis(Freetype, font->Outlines, msdfgen::FontVariationAxis::Tag("wght"), 400.0);
						Log(LogLevel::Info, std::format("Text: {} drawn at weight 400 (its default is {})", file,
							axes[axis].default_value));
					}
				hb_font_extents_t extents{};
				hb_font_get_h_extents(font->Shaper, &extents);
				font->Ascender = static_cast<float>(extents.ascender / font->UnitsPerEm);
				font->Descender = static_cast<float>(extents.descender / font->UnitsPerEm);
				font->LineGap = static_cast<float>(extents.line_gap / font->UnitsPerEm);
				Fonts.push_back(std::move(font));
			}
			Log(LogLevel::Info, std::format("Text: {} fonts, ICU {}, HarfBuzz {}", Fonts.size(), U_ICU_VERSION,
				hb_version_string()));
			// Printable ASCII is generated in the background from the start (UIDesign.md section 8), so most
			// UI text is ready by its first frame; other glyphs follow on first use.
			for (char32_t codepoint = 0x20; codepoint < 0x7F; ++codepoint)
			{
				hb_codepoint_t glyph = 0;
				if (hb_font_get_nominal_glyph(Fonts.front()->Shaper, codepoint, &glyph))
					Request(0, glyph);
			}
		}

		~Impl()
		{
			// Background glyph jobs read the fonts; wait for every one before they go.
			Workers.Wait(Outstanding);
			Fonts.clear();
			if (Freetype)
				msdfgen::deinitializeFreetype(Freetype);
		}

		std::uint16_t FontFor(char32_t codepoint, std::uint16_t previous, bool mark) const noexcept
		{
			// Combining marks stay with their base's font, so a cluster is never split across fonts.
			if (mark)
				return previous;
			for (std::uint16_t index = 0; index < Fonts.size(); ++index)
			{
				hb_codepoint_t glyph = 0;
				if (hb_font_get_nominal_glyph(Fonts[index]->Shaper, codepoint, &glyph))
					return index;
			}
			return previous;
		}

		// Shapes one paragraph. Safe on any thread: ICU objects and HarfBuzz buffers are local; fonts
		// are only read.
		std::unique_ptr<Shaped> Shape(std::string_view utf8, TextDirection direction) const
		{
			auto shaped = std::make_unique<Shaped>();
			UErrorCode status = U_ZERO_ERROR;
			std::int32_t length = 0;
			u_strFromUTF8WithSub(nullptr, 0, &length, utf8.data(), static_cast<std::int32_t>(utf8.size()), 0xFFFD, nullptr,
				&status);
			status = U_ZERO_ERROR;
			shaped->Text.resize(static_cast<std::size_t>(length));
			u_strFromUTF8WithSub(shaped->Text.data(), length, nullptr, utf8.data(), static_cast<std::int32_t>(utf8.size()),
				0xFFFD, nullptr, &status);
			if (U_FAILURE(status))
				throw std::runtime_error(std::string("text is not UTF-8: ") + u_errorName(status));
			const auto* text = shaped->Text.data();
			shaped->BreakBefore.assign(shaped->Text.size() + 1, 0);
			shaped->Advance.assign(shaped->Text.size() + 1, 0.0f);
			if (length == 0)
				return shaped;

			// Line-break opportunities (UAX #14, with the dictionaries for Thai, Lao, Khmer and Burmese).
			if (UBreakIterator* breaks = ubrk_open(UBRK_LINE, "", text, length, &status); U_SUCCESS(status))
			{
				for (auto position = ubrk_first(breaks); position != UBRK_DONE; position = ubrk_next(breaks))
					shaped->BreakBefore[static_cast<std::size_t>(position)] = 1;
				ubrk_close(breaks);
			}
			status = U_ZERO_ERROR;

			// Bidirectional levels (UAX #9) for the paragraph direction the UI sets.
			UBiDi* bidi = ubidi_openSized(length, 0, &status);
			ubidi_setPara(bidi, text, length, direction == TextDirection::RightToLeft ? 1 : 0, nullptr, &status);
			if (U_FAILURE(status))
			{
				ubidi_close(bidi);
				throw std::runtime_error(std::string("bidi failed: ") + u_errorName(status));
			}

			// Runs of one level, one script and one font.
			struct Item { std::int32_t Start; std::int32_t End; std::uint8_t Level; UScriptCode Script; std::uint16_t Font; };
			std::vector<Item> items;
			UScriptCode script = USCRIPT_COMMON;
			std::uint16_t font = 0;
			for (std::int32_t index = 0; index < length;)
			{
				const std::int32_t start = index;
				UChar32 codepoint = 0;
				U16_NEXT(text, index, length, codepoint);
				const auto level = static_cast<std::uint8_t>(ubidi_getLevelAt(bidi, start));
				UErrorCode scriptStatus = U_ZERO_ERROR;
				UScriptCode found = uscript_getScript(codepoint, &scriptStatus);
				const bool mark = found == USCRIPT_INHERITED;
				if (found == USCRIPT_COMMON || found == USCRIPT_INHERITED)
					found = script;
				const auto chosen = FontFor(static_cast<char32_t>(codepoint), font, mark);
				if (items.empty() || items.back().Level != level || (found != script && script != USCRIPT_COMMON) ||
					chosen != font)
				{
					// A run that began with only common characters adopts the first real script.
					if (!items.empty() && items.back().Level == level && items.back().Font == chosen &&
						items.back().Script == USCRIPT_COMMON)
						items.back().Script = found;
					else
						items.push_back({ start, index, level, found, chosen });
				}
				items.back().End = index;
				if (items.back().Script == USCRIPT_COMMON)
					items.back().Script = found;
				script = found;
				font = chosen;
			}
			ubidi_close(bidi);

			hb_buffer_t* buffer = hb_buffer_create();
			for (const auto& item : items)
			{
				hb_buffer_clear_contents(buffer);
				hb_buffer_add_utf16(buffer, reinterpret_cast<const std::uint16_t*>(text), length,
					static_cast<unsigned>(item.Start), item.End - item.Start);
				hb_buffer_set_direction(buffer, (item.Level & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
				hb_buffer_set_script(buffer, item.Script == USCRIPT_COMMON ? HB_SCRIPT_COMMON :
					hb_script_from_string(uscript_getShortName(item.Script), -1));
				hb_buffer_set_language(buffer, hb_language_get_default());
				const auto& face = *Fonts[item.Font];
				hb_shape(face.Shaper, buffer, nullptr, 0);
				unsigned count = 0;
				const auto* infos = hb_buffer_get_glyph_infos(buffer, &count);
				const auto* positions = hb_buffer_get_glyph_positions(buffer, &count);
				ShapedRun run;
				run.First = static_cast<std::uint32_t>(shaped->Glyphs.size());
				run.Count = count;
				run.Start = static_cast<std::uint32_t>(item.Start);
				run.End = static_cast<std::uint32_t>(item.End);
				run.Level = item.Level;
				const auto scale = static_cast<float>(1.0 / face.UnitsPerEm);
				for (unsigned index = 0; index < count; ++index)
				{
					ShapedGlyph glyph;
					glyph.Font = item.Font;
					glyph.Glyph = infos[index].codepoint;
					glyph.Advance = positions[index].x_advance * scale;
					glyph.OffsetX = positions[index].x_offset * scale;
					glyph.OffsetY = positions[index].y_offset * scale;
					glyph.Cluster = infos[index].cluster;
					shaped->Advance[glyph.Cluster] += glyph.Advance;
					shaped->Glyphs.push_back(glyph);
				}
				shaped->Runs.push_back(run);
			}
			hb_buffer_destroy(buffer);
			return shaped;
		}

		Shaped& ShapedFor(std::string_view utf8, TextDirection direction)
		{
			const auto key = Hash(utf8, static_cast<std::uint64_t>(direction));
			auto found = ShapedCache.find(key);
			if (found == ShapedCache.end())
				found = ShapedCache.emplace(key, Shape(utf8, direction)).first;
			found->second->LastUsed = Frame;
			return *found->second;
		}

		TextBlock LayoutShaped(const Shaped& shaped, const TextStyle& style) const
		{
			TextBlock block;
			block.Size = style.Size;
			const auto& primary = *Fonts.front();
			const float size = style.Size;
			const float lineHeight = style.LineHeight > 0.0f ? style.LineHeight * size :
				(primary.Ascender - primary.Descender + primary.LineGap) * size;
			const float ascent = primary.Ascender * size + 0.5f * (lineHeight - (primary.Ascender - primary.Descender) * size);
			const float wrap = style.WrapWidth > 0.0f ? style.WrapWidth / size : 0.0f;

			// Lines as UTF-16 ranges, breaking at the last opportunity that fits.
			const auto& text = shaped.Text;
			const auto length = static_cast<std::uint32_t>(text.size());
			std::vector<std::pair<std::uint32_t, std::uint32_t>> lines;
			std::uint32_t start = 0;
			float width = 0.0f;
			std::uint32_t lastBreak = 0;
			for (std::uint32_t index = 0; index < length; ++index)
			{
				if (index > start && shaped.BreakBefore[index])
					lastBreak = index;
				if (text[index] == u'\n')
				{
					lines.emplace_back(start, index);
					start = index + 1;
					width = 0.0f;
					lastBreak = 0;
					continue;
				}
				width += shaped.Advance[index];
				if (wrap > 0.0f && width > wrap && lastBreak > start)
				{
					lines.emplace_back(start, lastBreak);
					start = lastBreak;
					width = 0.0f;
					for (std::uint32_t inner = start; inner <= index; ++inner)
						width += shaped.Advance[inner];
					lastBreak = 0;
				}
			}
			lines.emplace_back(start, length);

			struct Line { float Width; std::size_t First; std::size_t End; };
			std::vector<Line> placed;
			for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
			{
				auto [lineStart, lineEnd] = lines[lineIndex];
				// Trailing spaces neither take width nor shift aligned text.
				while (lineEnd > lineStart && (text[lineEnd - 1] == u' ' || text[lineEnd - 1] == u'\t'))
					--lineEnd;
				// Pieces of runs in this line, then their visual order from the bidi levels.
				struct Piece { const ShapedRun* Run; std::uint32_t Start; std::uint32_t End; };
				std::vector<Piece> pieces;
				std::vector<UBiDiLevel> levels;
				for (const auto& run : shaped.Runs)
					if (run.End > lineStart && run.Start < lineEnd)
					{
						pieces.push_back({ &run, std::max(run.Start, lineStart), std::min(run.End, lineEnd) });
						levels.push_back(run.Level);
					}
				std::vector<std::int32_t> order(pieces.size());
				if (!pieces.empty())
					ubidi_reorderVisual(levels.data(), static_cast<std::int32_t>(levels.size()), order.data());
				const float baseline = ascent + static_cast<float>(lineIndex) * lineHeight;
				float pen = 0.0f;
				const std::size_t first = block.Glyphs.size();
				for (const auto logical : order)
				{
					const auto& piece = pieces[static_cast<std::size_t>(logical)];
					for (std::uint32_t index = 0; index < piece.Run->Count; ++index)
					{
						const auto& glyph = shaped.Glyphs[piece.Run->First + index];
						if (glyph.Cluster < piece.Start || glyph.Cluster >= piece.End)
							continue;
						if (glyph.Glyph != 0 || text[glyph.Cluster] != u'\n')
							block.Glyphs.push_back({ glyph.Font, glyph.Glyph, (pen + glyph.OffsetX) * size,
								baseline - glyph.OffsetY * size });
						pen += glyph.Advance;
					}
				}
				placed.push_back({ pen * size, first, block.Glyphs.size() });
				block.Width = std::max(block.Width, pen * size);
			}
			block.Lines = static_cast<std::uint32_t>(placed.size());
			block.Height = static_cast<float>(placed.size()) * lineHeight;
			// Alignment within the block; start is the left edge for left-to-right paragraphs.
			const float boxWidth = style.WrapWidth > 0.0f ? std::max(block.Width, style.WrapWidth) : block.Width;
			for (const auto& line : placed)
			{
				const float slack = boxWidth - line.Width;
				float shift = 0.0f;
				const bool rtl = style.Direction == TextDirection::RightToLeft;
				if (style.Align == TextAlignment::Center)
					shift = slack * 0.5f;
				else if ((style.Align == TextAlignment::End) != rtl)
					shift = slack;
				for (std::size_t index = line.First; index < line.End; ++index)
					block.Glyphs[index].X += shift;
			}
			if (style.WrapWidth > 0.0f)
				block.Width = boxWidth;
			return block;
		}

		// Background job: one glyph's outline into an MTSDF bitmap.
		struct GlyphJob
		{
			Impl* Owner = nullptr;
			std::uint64_t Key = 0;
			Font* Face = nullptr;
			std::uint32_t Glyph = 0;
		};

		static void Generate(void* context, std::uint32_t) noexcept
		{
			std::unique_ptr<GlyphJob> job(static_cast<GlyphJob*>(context));
			FinishedGlyph result;
			result.Key = job->Key;
			try
			{
				msdf_atlas::GlyphGeometry geometry;
				bool loaded = false;
				{
					std::lock_guard lock(job->Face->OutlineMutex);
					loaded = geometry.load(job->Face->Outlines, 1.0 / job->Face->UnitsPerEm,
						msdfgen::GlyphIndex(job->Glyph), false);
				}
				if (!loaded || geometry.isWhitespace())
					result.Empty = true;
				else
				{
					geometry.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, 0);
					geometry.wrapBox(AtlasEm, AtlasRange / AtlasEm, 1.0);
					int width = 0;
					int height = 0;
					geometry.getBoxSize(width, height);
					double l = 0, b = 0, r = 0, t = 0;
					geometry.getQuadPlaneBounds(l, b, r, t);
					if (width <= 0 || height <= 0)
						result.Empty = true;
					else if (width > 512 || height > 512)
						result.Failed = true;
					else
					{
						msdfgen::Bitmap<float, 4> bitmap(width, height);
						msdf_atlas::GeneratorAttributes attributes;
						// Variable fonts overlap their contours; the scanline pass resolves the fill.
						attributes.scanlinePass = true;
						attributes.config.overlapSupport = true;
						msdf_atlas::mtsdfGenerator(bitmap, geometry, attributes);
						result.Width = static_cast<std::uint32_t>(width);
						result.Height = static_cast<std::uint32_t>(height);
						result.Pixels.resize(std::size_t{ result.Width } * result.Height * 4);
						for (int y = 0; y < height; ++y)
							for (int x = 0; x < width; ++x)
							{
								// msdfgen rows run bottom-up; pages run top-down.
								const float* texel = bitmap(x, height - 1 - y);
								auto* out = result.Pixels.data() + (std::size_t(y) * result.Width + std::size_t(x)) * 4;
								for (int channel = 0; channel < 4; ++channel)
									out[channel] = static_cast<std::byte>(std::lround(std::clamp(texel[channel], 0.0f, 1.0f) * 255.0f));
							}
						result.Plane[0] = static_cast<float>(l);
						result.Plane[1] = static_cast<float>(b);
						result.Plane[2] = static_cast<float>(r);
						result.Plane[3] = static_cast<float>(t);
					}
				}
			}
			catch (...)
			{
				result.Failed = true;
			}
			Impl& owner = *job->Owner;
			{
				std::lock_guard lock(owner.FinishedMutex);
				owner.Finished.push_back(std::move(result));
			}
			if (owner.Outstanding.fetch_sub(1, std::memory_order_acq_rel) == 1)
				owner.Outstanding.notify_all();
		}

		GlyphEntry& Request(std::uint16_t font, std::uint32_t glyph)
		{
			const auto key = (std::uint64_t{ font } << 32) | glyph;
			auto [entry, added] = Glyphs.try_emplace(key);
			if (added)
			{
				Outstanding.fetch_add(1, std::memory_order_relaxed);
				Workers.Submit(&Generate, new GlyphJob{ this, key, Fonts[font].get(), glyph }, 0,
					JobSystem::Priority::Background);
			}
			return entry->second;
		}

		bool Place(FinishedGlyph& glyph, GlyphEntry& entry)
		{
			const std::uint32_t width = glyph.Width + GlyphGap;
			const std::uint32_t height = glyph.Height + GlyphGap;
			for (std::size_t attempt = 0; attempt < 2; ++attempt)
			{
				if (!Pages.empty())
				{
					auto& page = Pages.back();
					if (page.CursorX + width > PageSize)
					{
						page.ShelfY += page.ShelfHeight;
						page.ShelfHeight = 0;
						page.CursorX = 0;
					}
					if (page.ShelfY + height <= PageSize)
					{
						const std::uint32_t x = page.CursorX;
						const std::uint32_t y = page.ShelfY;
						page.CursorX += width;
						page.ShelfHeight = std::max(page.ShelfHeight, height);
						entry.Page = static_cast<std::uint32_t>(Pages.size() - 1);
						const float inverse = 1.0f / static_cast<float>(PageSize);
						entry.Uv[0] = static_cast<float>(x) * inverse;
						entry.Uv[1] = static_cast<float>(y) * inverse;
						entry.Uv[2] = static_cast<float>(x + glyph.Width) * inverse;
						entry.Uv[3] = static_cast<float>(y + glyph.Height) * inverse;
						std::copy(std::begin(glyph.Plane), std::end(glyph.Plane), entry.Plane);
						Uploads.push_back({ page.Texture, x, y, glyph.Width, glyph.Height, std::move(glyph.Pixels) });
						return true;
					}
				}
				// A new page, cleared to "far outside" so filtering at glyph edges reads empty space.
				const std::vector<std::byte> clear(std::size_t{ PageSize } * PageSize * 4, std::byte{ 0 });
				Pages.push_back({ Renderer.CreateManagedTexture({ PageSize, PageSize, clear, TextureColorSpace::Linear }) });
				PageIndex.push_back(Renderer.GetBindlessIndex(Pages.back().Texture));
			}
			return false;
		}

		VulkanContext& Renderer;
		JobSystem& Workers;
		std::vector<std::byte> IcuData;
		msdfgen::FreetypeHandle* Freetype = nullptr;
		std::vector<std::unique_ptr<Font>> Fonts;
		std::unordered_map<std::uint64_t, std::unique_ptr<Shaped>> ShapedCache;
		std::unordered_map<std::uint64_t, CachedBlock> Blocks;
		std::unordered_map<std::uint64_t, GlyphEntry> Glyphs;
		std::vector<Page> Pages;
		std::vector<std::uint32_t> PageIndex;
		std::mutex FinishedMutex;
		std::vector<FinishedGlyph> Finished;
		std::atomic<std::uint32_t> Outstanding{ 0 };
		std::vector<PendingUpload> Uploads;
		std::vector<OverlayTextureUpload> UploadViews;
		std::size_t Listed = 0;
		std::uint64_t Frame = 1;
	};

	TextSystem::TextSystem(VulkanContext& renderer, JobSystem& workers, const std::filesystem::path& contentRoot)
		: m_Impl(std::make_unique<Impl>(renderer, workers, contentRoot))
	{
	}

	TextSystem::~TextSystem() = default;

	void TextSystem::Prepare(std::span<const std::string_view> texts, std::span<const TextStyle> styles)
	{
		auto& impl = *m_Impl;
		std::vector<std::pair<std::uint64_t, std::size_t>> misses;
		std::unordered_set<std::uint64_t> seen;
		for (std::size_t index = 0; index < texts.size() && index < styles.size(); ++index)
		{
			const auto key = Hash(texts[index], static_cast<std::uint64_t>(styles[index].Direction));
			if (!impl.ShapedCache.contains(key) && seen.insert(key).second)
				misses.emplace_back(key, index);
		}
		if (misses.empty())
			return;
		std::vector<std::unique_ptr<Shaped>> results(misses.size());
		impl.Workers.ParallelFor(static_cast<std::uint32_t>(misses.size()), [&](std::uint32_t index) noexcept
		{
			try
			{
				const auto source = misses[index].second;
				results[index] = impl.Shape(texts[source], styles[source].Direction);
			}
			catch (...)
			{
			}
		});
		for (std::size_t index = 0; index < misses.size(); ++index)
			if (results[index])
			{
				results[index]->LastUsed = impl.Frame;
				impl.ShapedCache.emplace(misses[index].first, std::move(results[index]));
			}
	}

	const TextBlock& TextSystem::Layout(std::string_view utf8, const TextStyle& style)
	{
		auto& impl = *m_Impl;
		std::uint64_t key = Hash(utf8, Bits(style.Size));
		key = key * 31 + Bits(style.LineHeight);
		key = key * 31 + Bits(style.WrapWidth);
		key = key * 31 + (static_cast<std::uint64_t>(style.Direction) << 2 | static_cast<std::uint64_t>(style.Align));
		auto found = impl.Blocks.find(key);
		if (found == impl.Blocks.end())
		{
			const auto& shaped = impl.ShapedFor(utf8, style.Direction);
			found = impl.Blocks.emplace(key, CachedBlock{ impl.LayoutShaped(shaped, style), 0 }).first;
		}
		found->second.LastUsed = impl.Frame;
		return found->second.Block;
	}

	bool TextSystem::AppendQuads(const TextBlock& block, float x, float y, const std::array<float, 4>& color,
		const std::array<float, 4>& clip, std::vector<OverlayQuad>& out)
	{
		auto& impl = *m_Impl;
		bool ready = true;
		for (const auto& glyph : block.Glyphs)
		{
			const auto& entry = impl.Request(glyph.Font, glyph.Glyph);
			if (entry.Status == GlyphEntry::State::Pending)
				ready = false;
		}
		if (!ready)
			return false;
		const float size = block.Size;
		const float range = static_cast<float>(AtlasRange * size / AtlasEm);
		for (const auto& glyph : block.Glyphs)
		{
			const auto& entry = impl.Glyphs[(std::uint64_t{ glyph.Font } << 32) | glyph.Glyph];
			if (entry.Status != GlyphEntry::State::Ready)
				continue;
			OverlayQuad quad;
			const float left = x + glyph.X + entry.Plane[0] * size;
			const float top = y + glyph.Baseline - entry.Plane[3] * size;
			quad.Rect[0] = left;
			quad.Rect[1] = top;
			quad.Rect[2] = (entry.Plane[2] - entry.Plane[0]) * size;
			quad.Rect[3] = (entry.Plane[3] - entry.Plane[1]) * size;
			std::copy(std::begin(entry.Uv), std::end(entry.Uv), quad.Uv);
			std::copy(color.begin(), color.end(), quad.Color);
			std::copy(clip.begin(), clip.end(), quad.Clip);
			quad.DistanceRange = std::max(range, 1.0f);
			quad.Texture = impl.PageIndex[entry.Page];
			quad.Flags = OverlayFlags::Glyph;
			out.push_back(quad);
		}
		return true;
	}

	std::span<const OverlayTextureUpload> TextSystem::TakeUploads()
	{
		auto& impl = *m_Impl;
		std::vector<FinishedGlyph> finished;
		{
			std::lock_guard lock(impl.FinishedMutex);
			finished.swap(impl.Finished);
		}
		for (auto& glyph : finished)
		{
			auto& entry = impl.Glyphs[glyph.Key];
			if (glyph.Failed)
				entry.Status = GlyphEntry::State::Failed;
			else if (glyph.Empty)
				entry.Status = GlyphEntry::State::Empty;
			else
				entry.Status = impl.Place(glyph, entry) ? GlyphEntry::State::Ready : GlyphEntry::State::Failed;
		}
		impl.UploadViews.clear();
		for (const auto& upload : impl.Uploads)
			impl.UploadViews.push_back({ upload.Texture, upload.X, upload.Y, upload.Width, upload.Height, upload.Pixels });
		impl.Listed = impl.Uploads.size();
		return impl.UploadViews;
	}

	void TextSystem::EndFrame(bool presented)
	{
		auto& impl = *m_Impl;
		if (presented)
			impl.Uploads.erase(impl.Uploads.begin(), impl.Uploads.begin() + static_cast<std::ptrdiff_t>(impl.Listed));
		impl.Listed = 0;
		++impl.Frame;
		if (impl.Frame % 60 == 0)
		{
			std::erase_if(impl.Blocks, [&](const auto& entry) { return impl.Frame - entry.second.LastUsed > CacheFrames; });
			std::erase_if(impl.ShapedCache, [&](const auto& entry) { return impl.Frame - entry.second->LastUsed > CacheFrames; });
		}
	}

	TextSystem::Stats TextSystem::GetStats() const
	{
		const auto& impl = *m_Impl;
		Stats stats;
		stats.Fonts = impl.Fonts.size();
		stats.Glyphs = impl.Glyphs.size();
		stats.PendingGlyphs = impl.Outstanding.load(std::memory_order_relaxed);
		stats.Pages = impl.Pages.size();
		stats.CachedTexts = impl.Blocks.size();
		return stats;
	}
}

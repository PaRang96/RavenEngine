#include "Raven/Scene/SceneFile.hpp"

#include "Raven/Assets/AssetId.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "ECS/FieldMatching.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace Raven
{
	namespace
	{
		constexpr std::uint32_t FormatVersion = 1;
		constexpr std::uint32_t EntitySection = 1;
		constexpr std::uint32_t ComponentSection = 2;
		// Section and string limits keep a corrupt length from allocating without bound.
		constexpr std::size_t MaxText = 1u << 16;
		constexpr std::size_t MaxFields = 1u << 12;

		using ECS::FieldKind;

		struct KindName
		{
			FieldKind Kind;
			std::string_view Name;
			// Bytes per element; 0 for Opaque, which accepts any size.
			std::uint32_t Size;
		};

		constexpr KindName Kinds[]{
			{ FieldKind::Bool, "bool", 1 }, { FieldKind::Char, "char", 1 }, { FieldKind::Int8, "i8", 1 },
			{ FieldKind::UInt8, "u8", 1 }, { FieldKind::Int16, "i16", 2 }, { FieldKind::UInt16, "u16", 2 },
			{ FieldKind::Int32, "i32", 4 }, { FieldKind::UInt32, "u32", 4 }, { FieldKind::Int64, "i64", 8 },
			{ FieldKind::UInt64, "u64", 8 }, { FieldKind::Float32, "f32", 4 }, { FieldKind::Float64, "f64", 8 },
			{ FieldKind::Vec3, "vec3", 12 }, { FieldKind::Vec4, "vec4", 16 }, { FieldKind::Mat4, "mat4", 64 },
			{ FieldKind::Entity, "entity", 16 }, { FieldKind::AssetId, "asset", 16 },
			{ FieldKind::AssetRef, "assetref", 16 }, { FieldKind::EntityRef, "entityref", 8 },
			{ FieldKind::Opaque, "opaque", 0 },
		};

		const KindName* FindKind(FieldKind kind) noexcept
		{
			for (const auto& entry : Kinds)
				if (entry.Kind == kind)
					return &entry;
			return nullptr;
		}

		const KindName* FindKind(std::string_view name) noexcept
		{
			for (const auto& entry : Kinds)
				if (entry.Name == name)
					return &entry;
			return nullptr;
		}

		std::uint32_t ElementCount(const SceneFile::Field& field) noexcept
		{
			return field.Extent[0] * field.Extent[1];
		}

		[[noreturn]] void Malformed(const std::string& reason)
		{
			throw std::runtime_error("Malformed scene file: " + reason);
		}

		void CheckField(const SceneFile::Field& field)
		{
			const auto* kind = FindKind(field.Kind);
			const std::uint64_t count = std::uint64_t{ field.Extent[0] } * field.Extent[1];
			if (!kind || field.Name.empty() || field.Size == 0 || count == 0 || field.Size % count != 0 ||
				(kind->Size != 0 && field.Size != kind->Size * count))
				Malformed("field '" + field.Name + "' has an unsupported kind or size");
		}

		void CheckComponent(const SceneFile::Component& component)
		{
			if (component.Type.empty() || component.Fields.size() > MaxFields)
				Malformed("component section without a type or with too many fields");
			std::uint64_t rowSize = 0;
			std::set<std::string_view> names;
			for (const auto& field : component.Fields)
			{
				CheckField(field);
				if (!names.insert(field.Name).second)
					Malformed("component '" + component.Type + "' repeats field '" + field.Name + "'");
				rowSize += field.Size;
			}
			if (component.Values.size() != rowSize * component.Entities.size())
				Malformed("component '" + component.Type + "' has the wrong amount of value data");
		}

		class Writer
		{
		public:
			void Bytes(const void* data, std::size_t size)
			{
				const auto* first = static_cast<const std::byte*>(data);
				m_Data.insert(m_Data.end(), first, first + size);
			}
			template<typename T>
			void Number(T value)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				Bytes(&value, sizeof(value));
			}
			void Text(std::string_view text)
			{
				if (text.size() > MaxText)
					throw std::length_error("Scene file text is too long");
				Number(static_cast<std::uint32_t>(text.size()));
				Bytes(text.data(), text.size());
			}
			std::size_t Size() const noexcept { return m_Data.size(); }
			void Patch(std::size_t at, std::uint64_t value) { std::memcpy(m_Data.data() + at, &value, sizeof(value)); }
			std::vector<std::byte>& Data() noexcept { return m_Data; }

		private:
			std::vector<std::byte> m_Data;
		};

		class Reader
		{
		public:
			explicit Reader(std::span<const std::byte> bytes) noexcept : m_Bytes(bytes) {}

			std::span<const std::byte> Take(std::size_t size)
			{
				if (size > m_Bytes.size() - m_Position)
					Malformed("unexpected end of data");
				const auto result = m_Bytes.subspan(m_Position, size);
				m_Position += size;
				return result;
			}
			template<typename T>
			T Number()
			{
				T value{};
				std::memcpy(&value, Take(sizeof(T)).data(), sizeof(T));
				return value;
			}
			std::string Text()
			{
				const auto size = Number<std::uint32_t>();
				if (size > MaxText)
					Malformed("text is too long");
				const auto bytes = Take(size);
				return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			}
			bool AtEnd() const noexcept { return m_Position == m_Bytes.size(); }
			std::size_t Remaining() const noexcept { return m_Bytes.size() - m_Position; }

		private:
			std::span<const std::byte> m_Bytes;
			std::size_t m_Position = 0;
		};

		SceneFile::Component DecodeComponent(Reader& reader)
		{
			SceneFile::Component component;
			component.Type = reader.Text();
			component.LayoutHash = reader.Number<std::uint64_t>();
			const auto fieldCount = reader.Number<std::uint32_t>();
			if (fieldCount > MaxFields)
				Malformed("too many fields");
			std::uint64_t rowSize = 0;
			for (std::uint32_t index = 0; index < fieldCount; ++index)
			{
				SceneFile::Field field;
				field.Name = reader.Text();
				field.Kind = static_cast<FieldKind>(reader.Number<std::uint8_t>());
				field.Flags = reader.Number<std::uint8_t>();
				reader.Number<std::uint16_t>();
				field.Extent[0] = reader.Number<std::uint32_t>();
				field.Extent[1] = reader.Number<std::uint32_t>();
				field.Tag = reader.Number<std::uint64_t>();
				field.Size = reader.Number<std::uint32_t>();
				CheckField(field);
				rowSize += field.Size;
				component.Fields.push_back(std::move(field));
			}
			const auto rows = reader.Number<std::uint32_t>();
			if (rows > reader.Remaining() / sizeof(std::uint64_t))
				Malformed("too many rows");
			component.Entities.resize(rows);
			for (auto& entity : component.Entities)
				entity = reader.Number<std::uint64_t>();
			if (rowSize != 0 && rows > reader.Remaining() / rowSize)
				Malformed("component values are truncated");
			const auto values = reader.Take(static_cast<std::size_t>(rowSize * rows));
			component.Values.assign(values.begin(), values.end());
			CheckComponent(component);
			return component;
		}

		std::string Hex(std::uint64_t value)
		{
			char text[17]{};
			const auto result = std::to_chars(text, text + 16, value, 16);
			std::string digits(text, result.ptr);
			return std::string(16 - digits.size(), '0') + digits;
		}

		std::string Quote(std::string_view text)
		{
			std::string quoted = "\"";
			for (const char character : text)
			{
				if (character == '"' || character == '\\')
					quoted += '\\';
				if (character == '\n')
					quoted += "\\n";
				else
					quoted += character;
			}
			return quoted + '"';
		}

		template<typename T>
		T Load(const std::byte* data) noexcept
		{
			T value{};
			std::memcpy(&value, data, sizeof(T));
			return value;
		}

		template<typename T>
		std::string Shortest(T value)
		{
			char text[64]{};
			const auto result = std::to_chars(text, text + sizeof(text), value);
			return std::string(text, result.ptr);
		}

		// Appends one element of a known kind as text tokens.
		void WriteElement(std::string& out, const KindName& kind, const std::byte* data, std::uint32_t size)
		{
			const auto floats = [&](std::size_t count)
			{
				for (std::size_t index = 0; index < count; ++index)
					out += ' ' + Shortest(Load<float>(data + index * sizeof(float)));
			};
			switch (kind.Kind)
			{
			case FieldKind::Bool: out += Load<std::uint8_t>(data) ? " 1" : " 0"; break;
			case FieldKind::Char: out += ' ' + std::to_string(Load<std::int8_t>(data)); break;
			case FieldKind::Int8: out += ' ' + std::to_string(Load<std::int8_t>(data)); break;
			case FieldKind::UInt8: out += ' ' + std::to_string(Load<std::uint8_t>(data)); break;
			case FieldKind::Int16: out += ' ' + std::to_string(Load<std::int16_t>(data)); break;
			case FieldKind::UInt16: out += ' ' + std::to_string(Load<std::uint16_t>(data)); break;
			case FieldKind::Int32: out += ' ' + std::to_string(Load<std::int32_t>(data)); break;
			case FieldKind::UInt32: out += ' ' + std::to_string(Load<std::uint32_t>(data)); break;
			case FieldKind::Int64: out += ' ' + std::to_string(Load<std::int64_t>(data)); break;
			case FieldKind::UInt64: out += ' ' + std::to_string(Load<std::uint64_t>(data)); break;
			case FieldKind::Float32: out += ' ' + Shortest(Load<float>(data)); break;
			case FieldKind::Float64: out += ' ' + Shortest(Load<double>(data)); break;
			case FieldKind::Vec3: floats(3); break;
			case FieldKind::Vec4: floats(4); break;
			case FieldKind::Mat4: floats(16); break;
			// A runtime handle means nothing in a file.
			case FieldKind::Entity: out += " -"; break;
			case FieldKind::AssetId:
			case FieldKind::AssetRef:
			{
				const auto id = Load<AssetId>(data);
				out += id.IsValid() ? ' ' + id.ToString() : std::string(" nil");
				break;
			}
			case FieldKind::EntityRef: out += ' ' + Hex(Load<std::uint64_t>(data)); break;
			case FieldKind::Opaque:
			{
				out += ' ';
				for (std::uint32_t index = 0; index < size; ++index)
				{
					constexpr char Digits[] = "0123456789abcdef";
					const auto byte = std::to_integer<unsigned>(data[index]);
					out += Digits[byte >> 4];
					out += Digits[byte & 15];
				}
				break;
			}
			}
		}

		class Tokens
		{
		public:
			explicit Tokens(std::string_view line) : m_Line(line) {}

			bool Empty()
			{
				SkipSpace();
				return m_Position >= m_Line.size();
			}

			std::string Next()
			{
				SkipSpace();
				if (m_Position >= m_Line.size())
					Malformed("missing value in text line: " + std::string(m_Line));
				if (m_Line[m_Position] != '"')
				{
					const auto start = m_Position;
					while (m_Position < m_Line.size() && m_Line[m_Position] != ' ' && m_Line[m_Position] != '\t')
						++m_Position;
					return std::string(m_Line.substr(start, m_Position - start));
				}
				std::string text;
				for (++m_Position; m_Position < m_Line.size(); ++m_Position)
				{
					const char character = m_Line[m_Position];
					if (character == '"')
					{
						++m_Position;
						return text;
					}
					if (character == '\\' && m_Position + 1 < m_Line.size())
					{
						const char escaped = m_Line[++m_Position];
						text += escaped == 'n' ? '\n' : escaped;
					}
					else
						text += character;
				}
				Malformed("unterminated text in line: " + std::string(m_Line));
			}

		private:
			void SkipSpace() noexcept
			{
				while (m_Position < m_Line.size() && (m_Line[m_Position] == ' ' || m_Line[m_Position] == '\t' ||
					m_Line[m_Position] == '\r'))
					++m_Position;
			}

			std::string_view m_Line;
			std::size_t m_Position = 0;
		};

		template<typename T>
		T ParseNumber(const std::string& token, int base = 10)
		{
			T value{};
			const auto* end = token.data() + token.size();
			std::from_chars_result result;
			if constexpr (std::is_floating_point_v<T>)
				result = std::from_chars(token.data(), end, value);
			else
				result = std::from_chars(token.data(), end, value, base);
			if (result.ec != std::errc{} || result.ptr != end)
				Malformed("invalid number '" + token + "'");
			return value;
		}

		template<typename T>
		void Store(std::byte* data, T value) noexcept
		{
			std::memcpy(data, &value, sizeof(T));
		}

		// Entity positions in breadth-first hierarchy order: roots, then each parent's children
		// together. Entities in a parent cycle come last, in file order.
		std::vector<std::size_t> HierarchyOrder(const SceneFile& file)
		{
			const auto count = file.Entities.size();
			std::unordered_map<std::uint64_t, std::size_t> index;
			index.reserve(count);
			for (std::size_t position = 0; position < count; ++position)
				index.emplace(file.Entities[position].Id, position);
			std::vector<std::vector<std::size_t>> children(count);
			std::vector<std::uint8_t> parented(count, 0);
			for (const auto& component : file.Components)
			{
				if (component.Type != ECS::ComponentTraits<ECS::ParentComponent>::Info.Name)
					continue;
				for (std::size_t field = 0; field < component.Fields.size(); ++field)
				{
					const auto& description = component.Fields[field];
					if (description.Name != "Parent" || description.Kind != FieldKind::EntityRef ||
						description.Size != sizeof(std::uint64_t))
						continue;
					const auto* values = component.Values.data() + component.FieldOffset(field);
					for (std::size_t row = 0; row < component.Entities.size(); ++row)
					{
						const auto child = index.find(component.Entities[row]);
						const auto parent = index.find(Load<std::uint64_t>(values + row * sizeof(std::uint64_t)));
						if (child == index.end() || parent == index.end() || child->second == parent->second)
							continue;
						children[parent->second].push_back(child->second);
						parented[child->second] = 1;
					}
				}
			}
			std::vector<std::size_t> order;
			order.reserve(count);
			std::vector<std::uint8_t> placed(count, 0);
			for (std::size_t position = 0; position < count; ++position)
				if (!parented[position])
				{
					order.push_back(position);
					placed[position] = 1;
				}
			for (std::size_t head = 0; head < order.size(); ++head)
				for (const auto child : children[order[head]])
					if (!placed[child])
					{
						order.push_back(child);
						placed[child] = 1;
					}
			for (std::size_t position = 0; position < count; ++position)
				if (!placed[position])
					order.push_back(position);
			return order;
		}

		void ReadElement(Tokens& tokens, const KindName& kind, std::byte* data, std::uint32_t size)
		{
			const auto floats = [&](std::size_t count)
			{
				for (std::size_t index = 0; index < count; ++index)
					Store(data + index * sizeof(float), ParseNumber<float>(tokens.Next()));
			};
			switch (kind.Kind)
			{
			case FieldKind::Bool: Store(data, static_cast<std::uint8_t>(ParseNumber<int>(tokens.Next()) != 0)); break;
			case FieldKind::Char:
			case FieldKind::Int8: Store(data, ParseNumber<std::int8_t>(tokens.Next())); break;
			case FieldKind::UInt8: Store(data, ParseNumber<std::uint8_t>(tokens.Next())); break;
			case FieldKind::Int16: Store(data, ParseNumber<std::int16_t>(tokens.Next())); break;
			case FieldKind::UInt16: Store(data, ParseNumber<std::uint16_t>(tokens.Next())); break;
			case FieldKind::Int32: Store(data, ParseNumber<std::int32_t>(tokens.Next())); break;
			case FieldKind::UInt32: Store(data, ParseNumber<std::uint32_t>(tokens.Next())); break;
			case FieldKind::Int64: Store(data, ParseNumber<std::int64_t>(tokens.Next())); break;
			case FieldKind::UInt64: Store(data, ParseNumber<std::uint64_t>(tokens.Next())); break;
			case FieldKind::Float32: Store(data, ParseNumber<float>(tokens.Next())); break;
			case FieldKind::Float64: Store(data, ParseNumber<double>(tokens.Next())); break;
			case FieldKind::Vec3: floats(3); break;
			case FieldKind::Vec4: floats(4); break;
			case FieldKind::Mat4: floats(16); break;
			case FieldKind::Entity:
				tokens.Next();
				std::memset(data, 0, size);
				break;
			case FieldKind::AssetId:
			case FieldKind::AssetRef:
			{
				const auto token = tokens.Next();
				Store(data, token == "nil" ? AssetId{} : AssetId::Parse(token));
				break;
			}
			case FieldKind::EntityRef: Store(data, ParseNumber<std::uint64_t>(tokens.Next(), 16)); break;
			case FieldKind::Opaque:
			{
				const auto token = tokens.Next();
				if (token.size() != std::size_t{ size } * 2)
					Malformed("opaque value '" + token + "' has the wrong length");
				for (std::uint32_t index = 0; index < size; ++index)
					data[index] = static_cast<std::byte>(ParseNumber<unsigned>(token.substr(index * 2, 2), 16));
				break;
			}
			}
		}
	}

	std::size_t SceneFile::Component::FieldOffset(std::size_t field) const noexcept
	{
		std::size_t offset = 0;
		for (std::size_t index = 0; index < field && index < Fields.size(); ++index)
			offset += std::size_t{ Fields[index].Size } * Entities.size();
		return offset;
	}

	bool IsSceneFile(std::span<const std::byte> bytes) noexcept
	{
		return bytes.size() >= SceneFileMagic.size() &&
			std::memcmp(bytes.data(), SceneFileMagic.data(), SceneFileMagic.size()) == 0;
	}

	void SceneFile::Normalize()
	{
		std::sort(Entities.begin(), Entities.end(), [](const Entity& left, const Entity& right) { return left.Id < right.Id; });
		std::sort(Components.begin(), Components.end(), [](const Component& left, const Component& right)
		{
			return left.Type < right.Type;
		});
		for (auto& component : Components)
		{
			const auto rows = component.Entities.size();
			std::vector<std::size_t> order(rows);
			std::iota(order.begin(), order.end(), std::size_t{ 0 });
			std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right)
			{
				return component.Entities[left] < component.Entities[right];
			});
			if (std::is_sorted(order.begin(), order.end()))
				continue;
			std::vector<std::uint64_t> entities(rows);
			std::vector<std::byte> values(component.Values.size());
			for (std::size_t row = 0; row < rows; ++row)
				entities[row] = component.Entities[order[row]];
			for (std::size_t field = 0; field < component.Fields.size(); ++field)
			{
				const auto size = component.Fields[field].Size;
				const auto base = component.FieldOffset(field);
				for (std::size_t row = 0; row < rows; ++row)
					std::memcpy(values.data() + base + row * size, component.Values.data() + base + order[row] * size, size);
			}
			component.Entities = std::move(entities);
			component.Values = std::move(values);
		}
	}

	SceneFile SceneFile::Decode(std::span<const std::byte> bytes)
	{
		if (!IsSceneFile(bytes))
			Malformed("missing the RVNSCENE magic");
		Reader reader(bytes.subspan(SceneFileMagic.size()));
		if (reader.Number<std::uint32_t>() != FormatVersion)
			throw std::runtime_error("Unsupported scene file version");
		const auto sections = reader.Number<std::uint32_t>();
		SceneFile file;
		std::set<std::uint64_t> ids;
		for (std::uint32_t index = 0; index < sections; ++index)
		{
			const auto kind = reader.Number<std::uint32_t>();
			reader.Number<std::uint32_t>();
			const auto length = reader.Number<std::uint64_t>();
			if (length > reader.Remaining())
				Malformed("section is truncated");
			Reader section(reader.Take(static_cast<std::size_t>(length)));
			if (kind == EntitySection)
			{
				const auto count = section.Number<std::uint32_t>();
				if (count > section.Remaining() / (sizeof(std::uint64_t) + sizeof(std::uint32_t)))
					Malformed("too many entities");
				for (std::uint32_t entity = 0; entity < count; ++entity)
				{
					Entity value;
					value.Id = section.Number<std::uint64_t>();
					value.Label = section.Text();
					if (value.Id == 0 || !ids.insert(value.Id).second)
						Malformed("entity ID is null or repeated");
					file.Entities.push_back(std::move(value));
				}
			}
			else if (kind == ComponentSection)
				file.Components.push_back(DecodeComponent(section));
			else
			{
				const auto all = section.Take(section.Remaining());
				file.Unknown.push_back({ kind, std::vector<std::byte>(all.begin(), all.end()) });
				continue;
			}
			if (!section.AtEnd())
				Malformed("section has trailing data");
		}
		if (!reader.AtEnd())
			Malformed("trailing data after the last section");
		return file;
	}

	std::vector<std::byte> SceneFile::Encode() const
	{
		SceneFile sorted = *this;
		sorted.Normalize();
		Writer writer;
		writer.Bytes(SceneFileMagic.data(), SceneFileMagic.size());
		writer.Number(FormatVersion);
		writer.Number(static_cast<std::uint32_t>(1 + sorted.Components.size() + sorted.Unknown.size()));
		const auto section = [&](std::uint32_t kind, const auto& body)
		{
			writer.Number(kind);
			writer.Number(std::uint32_t{ 0 });
			const auto length = writer.Size();
			writer.Number(std::uint64_t{ 0 });
			const auto start = writer.Size();
			body();
			writer.Patch(length, writer.Size() - start);
		};
		section(EntitySection, [&]
		{
			writer.Number(static_cast<std::uint32_t>(sorted.Entities.size()));
			for (const auto& entity : sorted.Entities)
			{
				writer.Number(entity.Id);
				writer.Text(entity.Label);
			}
		});
		for (const auto& component : sorted.Components)
		{
			CheckComponent(component);
			section(ComponentSection, [&]
			{
				writer.Text(component.Type);
				writer.Number(component.LayoutHash);
				writer.Number(static_cast<std::uint32_t>(component.Fields.size()));
				for (const auto& field : component.Fields)
				{
					writer.Text(field.Name);
					writer.Number(static_cast<std::uint8_t>(field.Kind));
					writer.Number(field.Flags);
					writer.Number(std::uint16_t{ 0 });
					writer.Number(field.Extent[0]);
					writer.Number(field.Extent[1]);
					writer.Number(field.Tag);
					writer.Number(field.Size);
				}
				writer.Number(static_cast<std::uint32_t>(component.Entities.size()));
				for (const auto entity : component.Entities)
					writer.Number(entity);
				writer.Bytes(component.Values.data(), component.Values.size());
			});
		}
		for (const auto& unknown : sorted.Unknown)
			section(unknown.Kind, [&] { writer.Bytes(unknown.Bytes.data(), unknown.Bytes.size()); });
		return std::move(writer.Data());
	}

	void SceneFile::Save(const std::filesystem::path& path) const
	{
		Files::WriteAtomic(path, Encode());
	}

	std::string SceneFile::ToText() const
	{
		SceneFile sorted = *this;
		sorted.Normalize();
		std::string out = "raven-scene-text 1\n";
		for (const auto& entity : sorted.Entities)
			out += "entity " + Hex(entity.Id) + (entity.Label.empty() ? "" : ' ' + Quote(entity.Label)) + '\n';
		for (const auto& component : sorted.Components)
		{
			CheckComponent(component);
			out += "component " + Quote(component.Type) + ' ' + Hex(component.LayoutHash) + '\n';
			for (const auto& field : component.Fields)
				out += "field " + Quote(field.Name) + ' ' + std::string(FindKind(field.Kind)->Name) + ' ' +
					std::to_string(field.Flags) + ' ' + std::to_string(field.Extent[0]) + ' ' +
					std::to_string(field.Extent[1]) + ' ' + Hex(field.Tag) + ' ' + std::to_string(field.Size) + '\n';
			for (std::size_t row = 0; row < component.Entities.size(); ++row)
			{
				out += "row " + Hex(component.Entities[row]);
				for (std::size_t index = 0; index < component.Fields.size(); ++index)
				{
					const auto& field = component.Fields[index];
					const auto& kind = *FindKind(field.Kind);
					const auto count = ElementCount(field);
					const auto element = field.Size / count;
					const auto* value = component.Values.data() + component.FieldOffset(index) + row * field.Size;
					out += " |";
					for (std::uint32_t item = 0; item < count; ++item)
						WriteElement(out, kind, value + item * element, element);
				}
				out += '\n';
			}
			out += "end\n";
		}
		for (const auto& unknown : sorted.Unknown)
		{
			out += "section " + std::to_string(unknown.Kind) + ' ';
			std::string hex;
			WriteElement(hex, *FindKind(FieldKind::Opaque), unknown.Bytes.data(), static_cast<std::uint32_t>(unknown.Bytes.size()));
			out += (hex.size() > 1 ? hex.substr(1) : std::string("-")) + '\n';
		}
		return out;
	}

	SceneFile SceneFile::FromText(std::string_view text)
	{
		SceneFile file;
		Component* current = nullptr;
		std::vector<std::vector<std::byte>> columns;
		std::set<std::uint64_t> ids;
		const auto finish = [&]
		{
			if (!current)
				return;
			for (const auto& column : columns)
				current->Values.insert(current->Values.end(), column.begin(), column.end());
			CheckComponent(*current);
			current = nullptr;
			columns.clear();
		};
		bool header = false;
		std::size_t position = 0;
		while (position <= text.size())
		{
			auto end = text.find('\n', position);
			if (end == std::string_view::npos)
				end = text.size();
			const auto line = text.substr(position, end - position);
			position = end + 1;
			Tokens tokens(line);
			if (tokens.Empty() || line.front() == '#')
				continue;
			const auto keyword = tokens.Next();
			if (!header)
			{
				if (keyword != "raven-scene-text" || tokens.Next() != "1")
					Malformed("missing the raven-scene-text 1 header");
				header = true;
				continue;
			}
			if (keyword == "entity")
			{
				Entity entity;
				entity.Id = ParseNumber<std::uint64_t>(tokens.Next(), 16);
				if (!tokens.Empty())
					entity.Label = tokens.Next();
				if (entity.Id == 0 || !ids.insert(entity.Id).second)
					Malformed("entity ID is null or repeated");
				file.Entities.push_back(std::move(entity));
			}
			else if (keyword == "component")
			{
				finish();
				current = &file.Components.emplace_back();
				current->Type = tokens.Next();
				current->LayoutHash = ParseNumber<std::uint64_t>(tokens.Next(), 16);
			}
			else if (keyword == "field" && current && current->Entities.empty())
			{
				Field field;
				field.Name = tokens.Next();
				const auto* kind = FindKind(tokens.Next());
				if (!kind)
					Malformed("unknown field kind in line: " + std::string(line));
				field.Kind = kind->Kind;
				field.Flags = ParseNumber<std::uint8_t>(tokens.Next());
				field.Extent[0] = ParseNumber<std::uint32_t>(tokens.Next());
				field.Extent[1] = ParseNumber<std::uint32_t>(tokens.Next());
				field.Tag = ParseNumber<std::uint64_t>(tokens.Next(), 16);
				field.Size = ParseNumber<std::uint32_t>(tokens.Next());
				CheckField(field);
				current->Fields.push_back(std::move(field));
				columns.emplace_back();
			}
			else if (keyword == "row" && current)
			{
				current->Entities.push_back(ParseNumber<std::uint64_t>(tokens.Next(), 16));
				for (std::size_t index = 0; index < current->Fields.size(); ++index)
				{
					const auto& field = current->Fields[index];
					if (tokens.Next() != "|")
						Malformed("expected '|' before each field in line: " + std::string(line));
					const auto count = ElementCount(field);
					const auto element = field.Size / count;
					auto& column = columns[index];
					const auto start = column.size();
					column.resize(start + field.Size);
					for (std::uint32_t item = 0; item < count; ++item)
						ReadElement(tokens, *FindKind(field.Kind), column.data() + start + item * element, element);
				}
				if (!tokens.Empty())
					Malformed("extra values in line: " + std::string(line));
			}
			else if (keyword == "end" && current)
				finish();
			else if (keyword == "section" && !current)
			{
				Section section;
				section.Kind = ParseNumber<std::uint32_t>(tokens.Next());
				const auto hex = tokens.Next();
				if (hex != "-")
				{
					if (hex.size() % 2 != 0)
						Malformed("section bytes have an odd length");
					for (std::size_t index = 0; index < hex.size(); index += 2)
						section.Bytes.push_back(static_cast<std::byte>(ParseNumber<unsigned>(hex.substr(index, 2), 16)));
				}
				file.Unknown.push_back(std::move(section));
			}
			else
				Malformed("unexpected line: " + std::string(line));
		}
		if (current)
			Malformed("component section without 'end'");
		if (!header)
			Malformed("empty text");
		return file;
	}

	namespace
	{
		// Each registered field takes the stored field of the same name or an old name, with the same
		// type (FieldMatching.hpp); any other field keeps its default (FD-16). Returns the stored
		// index per field, or -1.
		template<typename Report>
		std::vector<std::ptrdiff_t> MatchFields(const SceneFile::Component& component, const ECS::ComponentSchema& schema,
			const Report& report)
		{
			auto source = ECS::MatchSources(std::span<const SceneFile::Field>(component.Fields), schema,
				[](const SceneFile::Field& field) -> const std::string& { return field.Name; });
			std::vector<bool> used(component.Fields.size(), false);
			std::vector<std::string> missing;
			for (std::size_t index = 0; index < source.size(); ++index)
			{
				const auto& field = schema.Fields[index];
				if (source[index] < 0)
				{
					missing.push_back(field.Name);
					continue;
				}
				const auto stored = static_cast<std::size_t>(source[index]);
				used[stored] = true;
				if (!ECS::SameFieldType(component.Fields[stored], field))
				{
					report(component.Type + "::" + field.Name + " changed type; it takes its default value");
					source[index] = -1;
				}
			}
			bool dropped = false;
			for (std::size_t index = 0; index < component.Fields.size(); ++index)
				if (!used[index])
				{
					report(component.Type + "::" + component.Fields[index].Name + " is no longer a field; its value is not loaded");
					dropped = true;
				}
			// A new field taking its default is the normal path; beside a dropped stored field it is
			// probably a rename without an alias, so both are named.
			if (dropped)
				for (const auto& name : missing)
					report(component.Type + "::" + name + " is not in the stored data; it takes its default value");
			return source;
		}
	}

	// Friend of Scene and Registry: reads and writes whole pools.
	class SceneSerializer
	{
	public:
		static SceneFile Capture(const Scene& scene)
		{
			auto& registry = scene.m_Registry;
			SceneFile file;
			file.Entities.reserve(scene.m_Entities.size());
			for (const auto entity : scene.m_Entities)
				if (registry.IsAlive(entity))
				{
					const auto* node = scene.FindNode(entity);
					const auto label = scene.m_Labels.find(ECS::Detail::EntityAccess::Index(entity));
					file.Entities.push_back({ node->Id, label == scene.m_Labels.end() ? std::string{} : label->second });
				}
			for (const auto* schema : registry.GetSchemas())
			{
				if (schema->Flags & ECS::ComponentIsTransient)
					continue;
				const ECS::ComponentInfo info{ schema->Id, schema->Name, schema->Size, schema->Alignment, schema->LayoutHash };
				const auto pool = registry.GetPool(info);
				std::vector<std::pair<std::uint64_t, std::uint32_t>> rows;
				for (std::uint32_t dense = 0; dense < pool.Count; ++dense)
				{
					const auto entity = pool.Entities[dense];
					if (registry.GetOwner(entity) == scene.m_SceneId && registry.IsAlive(entity))
						if (const auto* node = scene.FindNode(entity))
							rows.emplace_back(node->Id, dense);
				}
				if (rows.empty())
					continue;
				std::sort(rows.begin(), rows.end());
				SceneFile::Component component;
				component.Type = schema->Name;
				component.LayoutHash = schema->LayoutHash;
				for (const auto& field : schema->Fields)
					component.Fields.push_back({ field.Name, field.Kind, field.Flags, field.Extent, field.Tag, field.Size });
				component.Entities.reserve(rows.size());
				for (const auto& [id, dense] : rows)
					component.Entities.push_back(id);
				component.Values.reserve(rows.size() * schema->Size);
				for (const auto& field : schema->Fields)
					for (const auto& [id, dense] : rows)
					{
						const auto* value = pool.Values + std::size_t{ dense } * pool.Stride + field.Offset;
						if (field.Kind == ECS::FieldKind::Entity)
							component.Values.insert(component.Values.end(), field.Size, std::byte{ 0 });
						else
							component.Values.insert(component.Values.end(), value, value + field.Size);
					}
				file.Components.push_back(std::move(component));
			}
			return file;
		}

		static void Instantiate(const SceneFile& file, Scene& scene)
		{
			auto& registry = scene.m_Registry;
			if (registry.IsIterating())
				throw std::logic_error("A scene file cannot be instantiated while a view iterates");
			std::set<std::string> reported;
			const auto report = [&reported](const std::string& message)
			{
				if (reported.insert(message).second)
					Log(LogLevel::Warning, "Scene file: " + message);
			};
			// Files list entities by random ID. Creating them parents first, with siblings together,
			// keeps nodes, depth buckets and component pools in hierarchy order, which the world
			// pass and the render world walk every frame.
			const auto order = HierarchyOrder(file);
			std::unordered_map<std::uint64_t, std::size_t> rank;
			rank.reserve(order.size());
			for (std::size_t position = 0; position < order.size(); ++position)
				rank.emplace(file.Entities[order[position]].Id, position);
			std::unordered_map<std::uint64_t, ECS::Entity> created;
			created.reserve(file.Entities.size());
			for (const auto index : order)
			{
				const auto& entity = file.Entities[index];
				auto label = entity.Label;
				if (!label.empty() && scene.m_Names.contains(label))
				{
					report("label '" + label + "' is used twice; the second entity has none");
					label.clear();
				}
				created.emplace(entity.Id, scene.CreateEntity(label, ECS::EntityRef{ entity.Id }));
			}
			const auto parentId = ECS::ComponentTraits<ECS::ParentComponent>::Info.Id;
			std::vector<std::pair<ECS::Entity, ECS::EntityRef>> parents;
			std::vector<std::byte> value;
			std::vector<std::pair<std::size_t, std::size_t>> keyed;
			for (const auto& component : file.Components)
			{
				// The registered type of this name, or the one that names it as an old name (FD-16).
				const auto* schema = registry.FindSchema(ECS::Detail::HashText(component.Type));
				if (!schema || schema->Name != component.Type)
				{
					schema = nullptr;
					for (const auto* candidate : registry.GetSchemas())
						if (std::find(candidate->OldNames.begin(), candidate->OldNames.end(), component.Type) !=
							candidate->OldNames.end())
							schema = candidate;
				}
				if (!schema)
				{
					report("component " + component.Type + " is not registered; its data is not loaded");
					continue;
				}
				if (schema->Flags & ECS::ComponentIsTransient)
					continue;
				const auto source = MatchFields(component, *schema, report);
				const ECS::ComponentInfo info{ schema->Id, schema->Name, schema->Size, schema->Alignment, schema->LayoutHash };
				// One lookup per row, then a sort of (position, row) pairs: looking positions up inside
				// the comparison cost about 0.9 s on the 100k-mesh scene.
				keyed.clear();
				keyed.reserve(component.Entities.size());
				for (std::size_t row = 0; row < component.Entities.size(); ++row)
				{
					const auto found = rank.find(component.Entities[row]);
					keyed.emplace_back(found == rank.end() ? rank.size() + row : found->second, row);
				}
				std::sort(keyed.begin(), keyed.end());
				for (const auto& [position, row] : keyed)
				{
					const auto target = created.find(component.Entities[row]);
					if (target == created.end())
					{
						report(component.Type + " has a row for an entity the file does not list");
						continue;
					}
					value = schema->Defaults;
					for (std::size_t index = 0; index < source.size(); ++index)
					{
						const auto& field = schema->Fields[index];
						if (source[index] < 0 || field.Kind == ECS::FieldKind::Entity)
							continue;
						const auto stored = static_cast<std::size_t>(source[index]);
						std::memcpy(value.data() + field.Offset,
							component.Values.data() + component.FieldOffset(stored) + row * field.Size, field.Size);
					}
					if (schema->Id == parentId)
					{
						ECS::ParentComponent parent;
						std::memcpy(&parent, value.data(), sizeof(parent));
						parents.emplace_back(target->second, parent.Parent);
					}
					else if (!registry.AddComponent(target->second, info, value.data()))
						report(component.Type + " could not be added to an entity");
				}
			}
			for (const auto& [entity, reference] : parents)
			{
				const auto parent = scene.Resolve(reference);
				if (parent.IsNull())
				{
					report("a parent reference names no entity of the scene; the child stays a root");
					continue;
				}
				try
				{
					scene.SetParent(entity, parent);
				}
				catch (const std::invalid_argument& error)
				{
					report(std::string("a parent was rejected (") + error.what() + "); the child stays a root");
				}
			}
		}
	};

	SceneFile CaptureScene(const Scene& scene)
	{
		return SceneSerializer::Capture(scene);
	}

	bool MatchesSchema(std::string_view type, const ECS::ComponentSchema& schema) noexcept
	{
		return type == schema.Name || std::find(schema.OldNames.begin(), schema.OldNames.end(), type) != schema.OldNames.end();
	}

	SceneFile::Component CaptureRecord(const ECS::ComponentSchema& schema, const void* value, std::uint64_t entityId)
	{
		SceneFile::Component component;
		component.Type = schema.Name;
		component.LayoutHash = schema.LayoutHash;
		component.Entities.push_back(entityId);
		const auto* bytes = static_cast<const std::byte*>(value);
		for (const auto& field : schema.Fields)
		{
			component.Fields.push_back({ field.Name, field.Kind, field.Flags, field.Extent, field.Tag, field.Size });
			// Entity handles mean nothing outside the running world; files store EntityRef instead.
			if (field.Kind == ECS::FieldKind::Entity)
				component.Values.insert(component.Values.end(), field.Size, std::byte{ 0 });
			else
				component.Values.insert(component.Values.end(), bytes + field.Offset, bytes + field.Offset + field.Size);
		}
		return component;
	}

	bool ApplyRecord(const SceneFile::Component& component, const ECS::ComponentSchema& schema, std::uint64_t entityId,
		void* value, const std::function<void(const std::string&)>& report)
	{
		const auto row = std::find(component.Entities.begin(), component.Entities.end(), entityId);
		if (row == component.Entities.end() || !MatchesSchema(component.Type, schema))
			return false;
		const auto index = static_cast<std::size_t>(row - component.Entities.begin());
		const auto source = MatchFields(component, schema, report ? report : [](const std::string&) {});
		auto* bytes = static_cast<std::byte*>(value);
		std::memcpy(bytes, schema.Defaults.data(), schema.Size);
		for (std::size_t field = 0; field < source.size(); ++field)
		{
			const auto& target = schema.Fields[field];
			if (source[field] < 0 || target.Kind == ECS::FieldKind::Entity)
				continue;
			std::memcpy(bytes + target.Offset,
				component.Values.data() + component.FieldOffset(static_cast<std::size_t>(source[field])) +
					index * target.Size, target.Size);
		}
		return true;
	}

	void InstantiateScene(const SceneFile& file, Scene& scene)
	{
		SceneSerializer::Instantiate(file, scene);
	}
}

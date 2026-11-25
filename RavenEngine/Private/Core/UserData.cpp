#include "Core/UserData.hpp"

#include "Raven/Core/Files.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/ECS/Registry.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <stdexcept>
#include <system_error>

namespace Raven
{
	namespace
	{
		// Binary user-data files: magic, version, reserved, a checksum of the payload, then the
		// payload, which is an encoded SceneFile (docs/GameServicesDesign.md, FD-33).
		constexpr std::string_view Magic = "RVNUDATA";
		constexpr std::uint32_t Version = 1;
		constexpr std::size_t HeaderSize = 8 + 4 + 4 + 8;
		constexpr std::string_view BinaryExtension = ".ravendata";
		constexpr std::string_view TextExtension = ".ravendata.txt";

		std::uint64_t Checksum(std::span<const std::byte> bytes) noexcept
		{
			return ECS::Detail::HashText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		}

		template<typename T>
		void Append(std::vector<std::byte>& out, T value)
		{
			const auto* bytes = reinterpret_cast<const std::byte*>(&value);
			out.insert(out.end(), bytes, bytes + sizeof(T));
		}

		template<typename T>
		T Take(std::span<const std::byte> bytes, std::size_t offset)
		{
			T value{};
			std::memcpy(&value, bytes.data() + offset, sizeof(T));
			return value;
		}

		void CheckName(std::string_view name)
		{
			const bool valid = !name.empty() && name.size() <= 64 &&
				std::all_of(name.begin(), name.end(), [](char character)
				{
					return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
						(character >= '0' && character <= '9') || character == '-' || character == '_';
				});
			if (!valid)
				throw std::invalid_argument("User data file names use 1-64 letters, digits, '-' or '_': " + std::string(name));
		}

		std::filesystem::path WithExtension(const std::filesystem::path& root, std::string_view name, UserDataFormat format)
		{
			return root / (std::string(name) + std::string(format == UserDataFormat::Text ? TextExtension : BinaryExtension));
		}

		std::filesystem::path Backup(const std::filesystem::path& path)
		{
			auto backup = path;
			backup += ".bak";
			return backup;
		}

		SceneFile Parse(std::span<const std::byte> bytes, UserDataFormat format)
		{
			if (format == UserDataFormat::Text)
				return SceneFile::FromText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
			if (bytes.size() < HeaderSize || std::memcmp(bytes.data(), Magic.data(), Magic.size()) != 0)
				throw std::runtime_error("not a user data file");
			if (Take<std::uint32_t>(bytes, 8) != Version)
				throw std::runtime_error("unsupported user data version");
			const auto payload = bytes.subspan(HeaderSize);
			if (Take<std::uint64_t>(bytes, 16) != Checksum(payload))
				throw std::runtime_error("checksum mismatch");
			return SceneFile::Decode(payload);
		}

		// One single-row section per row, in the stored field layout.
		std::vector<SceneFile::Component> SplitRows(const SceneFile::Component& section)
		{
			std::vector<SceneFile::Component> rows;
			for (std::size_t row = 0; row < section.Entities.size(); ++row)
			{
				SceneFile::Component single;
				single.Type = section.Type;
				single.LayoutHash = section.LayoutHash;
				single.Fields = section.Fields;
				single.Entities.push_back(section.Entities[row]);
				for (std::size_t field = 0; field < section.Fields.size(); ++field)
				{
					const auto size = section.Fields[field].Size;
					const auto* begin = section.Values.data() + section.FieldOffset(field) + row * size;
					single.Values.insert(single.Values.end(), begin, begin + size);
				}
				rows.push_back(std::move(single));
			}
			return rows;
		}

		std::string Timestamp()
		{
			const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
			return std::format("{:%Y%m%d-%H%M%S}", now);
		}
	}

	std::uint64_t GetUserDataKeyId(std::string_view key) noexcept
	{
		if (key.empty())
			return 1;
		const auto id = ECS::Detail::HashText(key);
		return id > 1 ? id : id + 2;
	}

	UserDataService::UserDataService(const ECS::Registry& registry, std::filesystem::path root)
		: m_Registry(registry), m_Root(std::move(root))
	{
	}

	void UserDataService::Report(std::string_view file, const std::string& message) const
	{
		const auto text = "User data '" + std::string(file) + "': " + message;
		if (m_Reported.insert(text).second)
			Log(LogLevel::Warning, text);
	}

	UserDataService::File& UserDataService::Open(std::string_view name) const
	{
		CheckName(name);
		auto found = m_Files.find(name);
		if (found == m_Files.end())
			found = m_Files.emplace(std::string(name), File{}).first;
		if (!found->second.Loaded)
		{
			found->second.Loaded = true;
			Load(name, found->second);
		}
		return found->second;
	}

	void UserDataService::Load(std::string_view name, File& file) const
	{
		// The format a SetFormat call asked for; a file found in the other format converts on its next save.
		const auto wanted = file.Format;
		const auto other = wanted == UserDataFormat::Text ? UserDataFormat::Binary : UserDataFormat::Text;
		// The current format first, then the other one, then each one's backup.
		const std::pair<std::filesystem::path, UserDataFormat> candidates[]{
			{ WithExtension(m_Root, name, file.Format), file.Format },
			{ WithExtension(m_Root, name, other), other },
			{ Backup(WithExtension(m_Root, name, file.Format)), file.Format },
			{ Backup(WithExtension(m_Root, name, other)), other } };
		for (std::size_t index = 0; index < std::size(candidates); ++index)
		{
			const auto& [path, format] = candidates[index];
			std::error_code error;
			if (!std::filesystem::is_regular_file(path, error))
				continue;
			try
			{
				const auto stored = Parse(Files::ReadBinary(path), format);
				std::map<std::uint64_t, std::string> labels;
				for (const auto& entity : stored.Entities)
					labels[entity.Id] = entity.Label;
				for (const auto& section : stored.Components)
					for (auto& row : SplitRows(section))
					{
						const auto label = labels.find(row.Entities.front());
						std::string key = label == labels.end() ? std::string{} : label->second;
						if (GetUserDataKeyId(key) != row.Entities.front())
						{
							Report(name, "a row names an entity with no matching key; it is kept but cannot be read");
							key = std::format("#{:016x}", row.Entities.front());
						}
						file.Records.push_back({ std::move(key), std::move(row) });
					}
				file.Unknown = stored.Unknown;
				file.Dirty = format != wanted;
				if (index >= 2)
				{
					Log(LogLevel::Warning, "User data '" + std::string(name) + "' recovered from " + path.string());
					file.Dirty = true;
				}
				return;
			}
			catch (const std::exception& failure)
			{
				// Set the damaged file aside so the next save cannot overwrite the evidence.
				auto aside = path;
				aside += ".damaged-" + Timestamp();
				for (int copy = 2; std::filesystem::exists(aside, error); ++copy)
				{
					aside = path;
					aside += ".damaged-" + Timestamp() + "-" + std::to_string(copy);
				}
				std::filesystem::rename(path, aside, error);
				Log(LogLevel::Warning, "User data " + path.string() + " is damaged (" + failure.what() + "); moved to " +
					aside.string() + (error ? " (move failed)" : "") + ". Its records take their defaults.");
				file.Records.clear();
				file.Unknown.clear();
			}
		}
	}

	bool UserDataService::Read(std::string_view name, std::string_view key, const ECS::ComponentInfo& info,
		void* value) const
	{
		const auto* schema = m_Registry.FindSchema(info.Id);
		if (!schema)
		{
			Report(name, std::string(info.Name) + " is not registered; declare it with SystemTable::Component<T>()");
			return false;
		}
		const auto& file = Open(name);
		const auto id = GetUserDataKeyId(key);
		for (const auto& record : file.Records)
			if (record.Key == key && ApplyRecord(record.Row, *schema, id, value,
				[&](const std::string& message) { Report(name, message); }))
				return true;
		return false;
	}

	void UserDataService::Write(std::string_view name, std::string_view key, const ECS::ComponentInfo& info,
		const void* value)
	{
		const auto* schema = m_Registry.FindSchema(info.Id);
		if (!schema)
			throw std::invalid_argument(std::string(info.Name) + " is not registered; declare it with SystemTable::Component<T>()");
		auto& file = Open(name);
		std::erase_if(file.Records, [&](const Record& record)
		{
			return record.Key == key && MatchesSchema(record.Row.Type, *schema);
		});
		file.Records.push_back({ std::string(key), CaptureRecord(*schema, value, GetUserDataKeyId(key)) });
		file.Dirty = true;
	}

	void UserDataService::Remove(std::string_view name, std::string_view key)
	{
		auto& file = Open(name);
		if (std::erase_if(file.Records, [&](const Record& record) { return record.Key == key; }) > 0)
			file.Dirty = true;
	}

	std::vector<std::string> UserDataService::GetKeys(std::string_view name) const
	{
		std::set<std::string> keys;
		for (const auto& record : Open(name).Records)
			keys.insert(record.Key);
		return { keys.begin(), keys.end() };
	}

	void UserDataService::SetFormat(std::string_view name, UserDataFormat format)
	{
		CheckName(name);
		auto found = m_Files.find(name);
		if (found == m_Files.end())
			found = m_Files.emplace(std::string(name), File{}).first;
		if (found->second.Format != format)
		{
			found->second.Format = format;
			// A loaded file switches on its next save; a file not yet loaded reads its new format first.
			found->second.Dirty = found->second.Loaded;
		}
	}

	std::filesystem::path UserDataService::GetPath(std::string_view name) const
	{
		CheckName(name);
		const auto found = m_Files.find(name);
		return WithExtension(m_Root, name, found == m_Files.end() ? UserDataFormat::Binary : found->second.Format);
	}

	std::vector<std::byte> UserDataService::Encode(const File& file) const
	{
		SceneFile stored;
		std::set<std::uint64_t> ids;
		for (const auto& record : file.Records)
		{
			const auto id = record.Row.Entities.front();
			if (ids.insert(id).second)
				stored.Entities.push_back({ id, record.Key.starts_with('#') ? std::string{} : record.Key });
			// Rows of one type and field layout share a section.
			const auto section = std::find_if(stored.Components.begin(), stored.Components.end(),
				[&](const SceneFile::Component& candidate)
				{
					return candidate.Type == record.Row.Type && candidate.Fields == record.Row.Fields;
				});
			if (section == stored.Components.end())
			{
				stored.Components.push_back(record.Row);
				continue;
			}
			// Field-major: insert this row's value at the end of each field's run.
			std::size_t offset = 0;
			std::vector<std::byte> values;
			for (std::size_t field = 0; field < section->Fields.size(); ++field)
			{
				const auto size = section->Fields[field].Size;
				const auto* run = section->Values.data() + section->FieldOffset(field);
				values.insert(values.end(), run, run + size * section->Entities.size());
				const auto* added = record.Row.Values.data() + offset;
				values.insert(values.end(), added, added + size);
				offset += size;
			}
			section->Entities.push_back(id);
			section->Values = std::move(values);
		}
		stored.Unknown = file.Unknown;
		if (file.Format == UserDataFormat::Text)
		{
			const auto text = stored.ToText();
			const auto* begin = reinterpret_cast<const std::byte*>(text.data());
			return { begin, begin + text.size() };
		}
		const auto payload = stored.Encode();
		std::vector<std::byte> bytes(Magic.size());
		std::memcpy(bytes.data(), Magic.data(), Magic.size());
		Append(bytes, Version);
		Append(bytes, std::uint32_t{ 0 });
		Append(bytes, Checksum(payload));
		bytes.insert(bytes.end(), payload.begin(), payload.end());
		return bytes;
	}

	bool UserDataService::Save(std::string_view name)
	{
		try
		{
			auto& file = Open(name);
			if (!file.Dirty)
				return true;
			const auto path = WithExtension(m_Root, name, file.Format);
			const auto bytes = Encode(file);
			std::filesystem::create_directories(m_Root);
			if (std::filesystem::is_regular_file(path))
				std::filesystem::copy_file(path, Backup(path), std::filesystem::copy_options::overwrite_existing);
			Files::WriteAtomic(path, bytes);
			// After a format change, the file in the old format would otherwise be read again later.
			std::error_code ignored;
			const auto other = file.Format == UserDataFormat::Text ? UserDataFormat::Binary : UserDataFormat::Text;
			std::filesystem::remove(WithExtension(m_Root, name, other), ignored);
			file.Dirty = false;
			return true;
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Error, "User data '" + std::string(name) + "' was not saved: " + error.what());
			return false;
		}
	}

	bool UserDataService::SaveAll()
	{
		bool saved = true;
		for (auto& [name, file] : m_Files)
			if (file.Dirty)
				saved = Save(name) && saved;
		return saved;
	}
}

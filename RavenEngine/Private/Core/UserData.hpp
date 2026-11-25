#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Core/UserData.hpp"
#include "Raven/Scene/SceneFile.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Raven
{
	namespace ECS
	{
		class Registry;
	}

	// The host's user-data store (docs/GameServicesDesign.md, FD-33). Files live under one folder per
	// game: the OS per-user folder for players, `out/User/<Game>` in development. Records are kept as
	// stored rows and matched against the registry's current field tables on every read, so a module
	// reload that changes a record type migrates it the same way scene files do.
	class UserDataService final : public UserDataStore
	{
	public:
		RAVEN_API UserDataService(const ECS::Registry& registry, std::filesystem::path root);
		UserDataService(const UserDataService&) = delete;
		UserDataService& operator=(const UserDataService&) = delete;

		RAVEN_API void Remove(std::string_view file, std::string_view key = {}) override;
		RAVEN_API std::vector<std::string> GetKeys(std::string_view file) const override;
		RAVEN_API void SetFormat(std::string_view file, UserDataFormat format) override;
		RAVEN_API bool Save(std::string_view file) override;
		// Saves every changed file; called by the host at shutdown.
		RAVEN_API bool SaveAll();
		const std::filesystem::path& GetRoot() const noexcept { return m_Root; }
		// The path a file is written to in its current format.
		RAVEN_API std::filesystem::path GetPath(std::string_view file) const;

		RAVEN_API bool Read(std::string_view file, std::string_view key, const ECS::ComponentInfo& info,
			void* value) const override;
		RAVEN_API void Write(std::string_view file, std::string_view key, const ECS::ComponentInfo& info,
			const void* value) override;

	private:
		struct Record
		{
			std::string Key;
			// One row, for the key's entity ID.
			SceneFile::Component Row;
		};
		struct File
		{
			std::vector<Record> Records;
			std::vector<SceneFile::Section> Unknown;
			UserDataFormat Format = UserDataFormat::Binary;
			bool Loaded = false;
			bool Dirty = false;
		};

		File& Open(std::string_view name) const;
		void Load(std::string_view name, File& file) const;
		std::vector<std::byte> Encode(const File& file) const;
		void Report(std::string_view file, const std::string& message) const;

		const ECS::Registry& m_Registry;
		std::filesystem::path m_Root;
		mutable std::map<std::string, File, std::less<>> m_Files;
		mutable std::set<std::string, std::less<>> m_Reported;
	};

	// The entity ID a key's records are stored under: 1 for the empty key.
	RAVEN_API std::uint64_t GetUserDataKeyId(std::string_view key) noexcept;
}

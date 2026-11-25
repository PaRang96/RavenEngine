#pragma once

#include "Raven/ECS/Component.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Raven
{
	enum class UserDataFormat : std::uint8_t
	{
		// The binary scene codec plus a checksum. Readable through `RavenSceneTool dump`.
		Binary,
		// The scene text form, for settings and bindings that people may read and fix by hand.
		Text,
	};

	// Per-user data that outlives a play session (docs/GameServicesDesign.md, FD-33): progress,
	// unlocks, best scores, settings. The host keeps it, outside the ECS and outside the game
	// module, so it survives module reloads.
	//
	// A file holds records. Each record is a value of a registered component type (declared with
	// SystemTable::Component<T>()), stored under an optional key, so one file can keep a record per
	// level, profile or slot. Stored fields migrate like scene files: by name, old name and type.
	//
	// File names use letters, digits, '-' and '_'. Changes stay in memory until Save; the host also
	// saves every changed file at shutdown. Writes are atomic and keep the previous file as `.bak`.
	// A damaged file is logged, set aside and read as empty, so records take their defaults.
	class UserDataStore
	{
	public:
		// The stored record, or the type's defaults when there is none.
		template<ECS::ComponentType T>
		T Get(std::string_view file, std::string_view key = {}) const
		{
			T value{};
			Read(file, key, ECS::ComponentTraits<T>::Info, &value);
			return value;
		}

		template<ECS::ComponentType T>
		bool Has(std::string_view file, std::string_view key = {}) const
		{
			T value{};
			return Read(file, key, ECS::ComponentTraits<T>::Info, &value);
		}

		template<ECS::ComponentType T>
		void Set(std::string_view file, const T& value)
		{
			Write(file, {}, ECS::ComponentTraits<T>::Info, &value);
		}

		template<ECS::ComponentType T>
		void Set(std::string_view file, std::string_view key, const T& value)
		{
			Write(file, key, ECS::ComponentTraits<T>::Info, &value);
		}

		// Removes every record stored under `key`.
		virtual void Remove(std::string_view file, std::string_view key = {}) = 0;
		// The keys that hold records, sorted; the empty key is included when it holds one.
		virtual std::vector<std::string> GetKeys(std::string_view file) const = 0;
		// Text or binary for the next save of `file`. Binary is the default.
		virtual void SetFormat(std::string_view file, UserDataFormat format) = 0;
		// Writes `file` if it changed. False, with one log line, when the write failed; the data
		// stays in memory and the next save tries again.
		virtual bool Save(std::string_view file) = 0;

	protected:
		~UserDataStore() = default;
		// `value` arrives holding the type's defaults. True when a stored record was applied.
		virtual bool Read(std::string_view file, std::string_view key, const ECS::ComponentInfo& info,
			void* value) const = 0;
		virtual void Write(std::string_view file, std::string_view key, const ECS::ComponentInfo& info,
			const void* value) = 0;
	};
}

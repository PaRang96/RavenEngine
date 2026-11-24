#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Raven
{
	class Scene;

	// The generic scene file (docs/DataModelDesign.md, FD-10). Every component section carries its
	// own field list, so a file written with an older component layout loads by matching field
	// names and types. Sections and fields the reader does not know are kept and written back
	// unchanged (FD-11). Encoding is deterministic: entities and rows sorted by ID, component
	// sections by type name, so saving an unchanged scene gives identical bytes.
	struct SceneFile
	{
		struct Entity
		{
			std::uint64_t Id = 0;
			// Optional; empty when the entity has none.
			std::string Label;
		};

		struct Field
		{
			std::string Name;
			ECS::FieldKind Kind = ECS::FieldKind::Opaque;
			std::uint8_t Flags = 0;
			std::array<std::uint32_t, 2> Extent{ 1, 1 };
			std::uint64_t Tag = 0;
			std::uint32_t Size = 0;

			bool operator==(const Field&) const = default;
		};

		struct Component
		{
			// The full type name, for example "Raven::ECS::TransformComponent".
			std::string Type;
			std::uint64_t LayoutHash = 0;
			std::vector<Field> Fields;
			// One row per entity, sorted by entity ID.
			std::vector<std::uint64_t> Entities;
			// Field-major: every row's first field, then every row's second field, and so on,
			// each value in its in-memory little-endian bytes.
			std::vector<std::byte> Values;

			RAVEN_API std::size_t FieldOffset(std::size_t field) const noexcept;
		};

		// A section of a kind this reader does not know, kept as it was.
		struct Section
		{
			std::uint32_t Kind = 0;
			std::vector<std::byte> Bytes;
		};

		std::vector<Entity> Entities;
		std::vector<Component> Components;
		std::vector<Section> Unknown;

		// Throws std::runtime_error for a malformed or unsupported file.
		RAVEN_API static SceneFile Decode(std::span<const std::byte> bytes);
		RAVEN_API std::vector<std::byte> Encode() const;
		RAVEN_API void Save(const std::filesystem::path& path) const;

		// Lossless text form for reviews and hand edits (`RavenSceneTool dump` and `apply`).
		RAVEN_API std::string ToText() const;
		RAVEN_API static SceneFile FromText(std::string_view text);

		// Sorts entities, rows and sections into the canonical order. Encode and ToText use it.
		RAVEN_API void Normalize();
	};

	inline constexpr std::string_view SceneFileMagic = "RVNSCENE";

	// True when the bytes start with the generic scene magic.
	RAVEN_API bool IsSceneFile(std::span<const std::byte> bytes) noexcept;

	// Copies the scene's live entities and every registered, non-transient component of them.
	RAVEN_API SceneFile CaptureScene(const Scene& scene);

	// Creates the file's entities with their stored IDs and adds their components, matching each
	// field by name and type against the registered layout. A component type, field or reference
	// the running code does not know is a data error: it is logged once and its value takes the
	// default (FD-06). Parents are applied last, through Scene::SetParent. Throws
	// std::invalid_argument when an entity ID is null or already used in the scene.
	RAVEN_API void InstantiateScene(const SceneFile& file, Scene& scene);
}

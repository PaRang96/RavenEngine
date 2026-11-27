#pragma once

#include "Raven/Assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>

namespace Raven
{
	enum class AssetKind : std::uint8_t
	{
		Unknown = 0,
		Texture = 1,
		Model = 2,
		Mesh = 3,
		Material = 4,
		Skeleton = 5,
		Animation = 6,
		Shader = 7,
		Scene = 8,
		Audio = 9,
		// A sound-event asset (.ravensound): clips plus how they play (docs/GameServicesDesign.md, C8).
		Sound = 10,
		// A TrueType or OpenType font (docs/UIDesign.md, AA2-MEDIA-05).
		Font = 11
	};

	struct AssetRecord
	{
		AssetId Id;
		AssetKind Kind = AssetKind::Unknown;
		// Content-relative source path; registration does not read the file.
		std::filesystem::path SourcePath;
	};
}

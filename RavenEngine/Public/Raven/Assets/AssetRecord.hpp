#pragma once

#include "Raven/Assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Raven
{
	// Where an asset's contents are (docs/RenderingContentDesign.md, FD-22). Assets a scene preloads are
	// Ready; an ID first used at runtime loads on a worker, and its holders draw a placeholder until then.
	enum class AssetLoadState : std::uint8_t
	{
		Ready,
		Loading,
		// The background load failed; the asset keeps its placeholder until its scene reloads.
		Failed,
	};

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
		// A part of a model file (docs/RenderingContentDesign.md, FD-23): the name path of its mesh node in
		// the file, as the sidecar's part table records it. Empty for the whole file.
		std::string Part;
	};
}

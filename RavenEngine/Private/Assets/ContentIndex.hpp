#pragma once

#include "Raven/Assets/AssetId.hpp"
#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Renderer/TextureDesc.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Raven
{
	// Asset identity without project records (docs/RenderingContentDesign.md, B0 contract 9, FD-23).
	// Imported files (PNG, FBX, audio) carry a `<file>.ravenasset` sidecar; Raven's own formats
	// (.ravenscene, .ravensound) carry their ID in their header. Development scans the content root;
	// a packaged player reads the index stage-player wrote into its project and never scans.

	// `<file>.ravenasset`, a tolerant text format (docs/DataModelDesign.md, FD-10):
	//   raven-asset 1
	//   id <uuid>
	//   kind texture|mesh|audio
	//   color-space srgb|linear      (textures)
	//   part "<name path>" <uuid>    (models: one per mesh node, FD-23)
	// Unknown keys are skipped with a warning, so later importer settings never break older readers.
	struct SidecarPart
	{
		std::string Path;
		AssetId Id;
	};

	struct Sidecar
	{
		AssetId Id;
		AssetKind Kind = AssetKind::Unknown;
		std::optional<TextureColorSpace> ColorSpace;
		// A model's parts in file order; a part no longer in the file keeps its line, so a rename can be
		// repaired by writing the new name path on it.
		std::vector<SidecarPart> Parts;
	};

	inline constexpr std::string_view SidecarExtension = ".ravenasset";

	RAVEN_API Sidecar ParseSidecar(std::span<const std::byte> bytes, const std::filesystem::path& path);
	RAVEN_API std::string FormatSidecar(const Sidecar& sidecar);
	// The sidecar beside a source file: `Cube.fbx` -> `Cube.fbx.ravenasset`.
	RAVEN_API std::filesystem::path SidecarPath(const std::filesystem::path& source);

	struct ContentAsset
	{
		AssetId Id;
		AssetKind Kind = AssetKind::Unknown;
		// Relative to the content root.
		std::filesystem::path Source;
		std::optional<TextureColorSpace> ColorSpace;
		// A model part's name path (FD-23); empty for the whole file.
		std::string Part;
	};

	struct PartTableUpdate
	{
		Sidecar Updated;
		// New parts that got IDs, and recorded parts the file no longer has.
		std::vector<SidecarPart> Added;
		std::vector<SidecarPart> Missing;
		bool Changed = false;
	};

	// Matches a model file's mesh nodes to its sidecar's part table by name path (FD-23): a known path
	// keeps its ID, a new one gets a new ID, and a recorded part the file lacks is kept and reported. A
	// file with one mesh keeps no table unless it already had one: its own ID is that mesh.
	RAVEN_API PartTableUpdate UpdatePartTable(const Sidecar& sidecar, const std::vector<std::string>& parts);
	// Reads the model file beside the sidecar and writes the sidecar when its table changes; the report
	// lines name every added and missing part.
	RAVEN_API PartTableUpdate ImportModel(const std::filesystem::path& source, std::vector<std::string>& report);
	// Repairs a rename (FD-23): the part recorded at `from` takes the name path `to`, which the file must
	// have, so references to its ID keep working; a line the importer added for `to` is dropped with its
	// new ID. Returns the report line.
	RAVEN_API std::string RenamePart(const std::filesystem::path& source, const std::string& from, const std::string& to);

	struct ContentIndex
	{
		// Sorted by source path.
		std::vector<ContentAsset> Assets;
		// Orphans, duplicates, files without an ID: logged by the caller, never fatal.
		std::vector<std::string> Problems;
	};

	// Every asset under `root`. With `createSidecars` (development), an imported file without a
	// sidecar gets one with a new ID, and the second of two sidecars sharing an ID (a copied file)
	// gets a new ID written back; otherwise both are only reported.
	RAVEN_API ContentIndex ScanContent(const std::filesystem::path& root, bool createSidecars);

	// What the scan reads, as bytes that change whenever an asset file appears, disappears or
	// changes: the reload service compares them on its audit, so new and edited sidecars reload
	// live without a watch per file (docs/HotReloadDesign.md, H1).
	RAVEN_API std::vector<std::byte> ContentFingerprint(const std::filesystem::path& root);
}

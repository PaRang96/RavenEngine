#pragma once

#include "Raven/Assets/AssetId.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Raven
{
	// One node of an imported model (docs/RenderingContentDesign.md, FD-23), for building its hierarchy as
	// entities: a TransformComponent from Position, Rotation and Scale, a ParentComponent naming the entity
	// of Parent, and, for a part, a StaticMeshComponent with its mesh ID.
	struct ModelNode
	{
		std::string Name;
		// The name path the sidecar's part table uses.
		std::string Path;
		// An earlier node of the list; -1 for a top-level node.
		std::int32_t Parent = -1;
		Vec3 Position{};
		// Radians, in TransformComponent's order.
		Vec3 Rotation{};
		Vec3 Scale{ 1.0f, 1.0f, 1.0f };
		// The part's mesh asset; nil for a node that only groups parts.
		AssetId Part;
	};

	struct ModelDescription
	{
		// The file's own ID: every part merged into one mesh, placed as in the model.
		AssetId Id;
		// Parents first.
		std::vector<ModelNode> Nodes;
	};

	// The model file at `source` (an FBX beside its sidecar), with each part's ID from the sidecar. A file
	// with one mesh is one node holding the file's own ID. Throws when the file has a part its sidecar does
	// not list yet: the development content scan, or RavenSceneTool import-model, adds it.
	RAVEN_API ModelDescription ReadModel(const std::filesystem::path& source);
}

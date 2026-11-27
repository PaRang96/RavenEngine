#pragma once

#include "Raven/Math/Transform.hpp"
#include "Raven/Renderer/ColorVertex.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Raven
{
	struct FbxMesh
	{
		std::vector<ColorVertex> Vertices;
		std::vector<std::uint32_t> Indices;
	};

	// One node of a model file (docs/RenderingContentDesign.md, FD-23): its name path, its parent in
	// the list (parents come first, -1 for a top-level node), its transform relative to the parent in
	// the engine's axes and metres, and whether it holds a mesh, which makes it a part.
	struct FbxNode
	{
		std::string Path;
		std::string Name;
		std::int32_t Parent = -1;
		Vec3 Position{};
		Quat Rotation{};
		Vec3 Scale{ 1.0f, 1.0f, 1.0f };
		bool Mesh = false;
	};

	std::vector<std::byte> ReadFbxBytes(const std::filesystem::path& path);
	// With an empty part: every mesh of the file merged in model space, which is what the file's own ID
	// draws (one mesh, as before B7). With a part's name path: that node's mesh in its own space, which an
	// entity with the node's transform places. Throws when the part is not in the file.
	FbxMesh DecodeFbxMesh(std::span<const std::byte> bytes, const std::filesystem::path& path, std::string_view part = {});
	// Every node on the way to a mesh, parents first, without reading geometry.
	std::vector<FbxNode> ReadFbxNodes(std::span<const std::byte> bytes, const std::filesystem::path& path);
	// The name paths of the file's mesh nodes, in file order.
	std::vector<std::string> ListFbxParts(std::span<const std::byte> bytes, const std::filesystem::path& path);
}

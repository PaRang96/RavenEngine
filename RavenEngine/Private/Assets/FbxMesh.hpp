#pragma once

#include "Raven/Renderer/ColorVertex.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Raven
{
	struct FbxMesh
	{
		std::vector<ColorVertex> Vertices;
		std::vector<std::uint16_t> Indices;
	};

	std::vector<std::byte> ReadFbxBytes(const std::filesystem::path& path);
	FbxMesh DecodeFbxMesh(std::span<const std::byte> bytes, const std::filesystem::path& path);
}

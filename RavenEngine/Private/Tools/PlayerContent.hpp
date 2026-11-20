#pragma once

#include "Raven/Scene/SceneProject.hpp"

#include <filesystem>
#include <span>

namespace Raven
{
	void StagePlayerContent(const SceneProjectDefinition& project, const std::filesystem::path& source,
		const std::filesystem::path& destination, std::span<const AssetId> scenes, std::span<const AssetId> extras);
}

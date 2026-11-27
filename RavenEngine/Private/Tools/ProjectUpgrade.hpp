#pragma once

#include <filesystem>
#include <string>

namespace Raven
{
	// `RavenSceneTool <root> upgrade-project` (docs/RenderingContentDesign.md, B0 contract 9): turns a
	// raven-project 2 file into version 3. Every imported asset gets its sidecar, every scene and
	// sound event gets its ID in its header, and the project keeps only its settings and scene
	// order. IDs never change, so every reference survives. The result is decoded again and compared
	// with the original catalog before the project file is replaced. Returns a summary.
	std::string UpgradeProject(const std::filesystem::path& root);

	// `RavenSceneTool <root> list`: the project's scenes and assets, one per line, for scripts.
	std::string ListProject(const std::filesystem::path& root);
}

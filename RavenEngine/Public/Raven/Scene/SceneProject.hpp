#pragma once

#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Assets/AssetStore.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Raven
{
	enum class SceneType { Perspective, Orthographic };

	struct SceneDescription
	{
		AssetId Id;
		SceneType Type;
		std::filesystem::path SourcePath;
	};

	struct SceneProjectDefinition
	{
		AssetCatalog Catalog;
		std::vector<SceneDescription> Scenes;
		std::vector<std::byte> SourceBytes;
	};

	class SceneProject
	{
	public:
		RAVEN_API static SceneProjectDefinition Decode(std::span<const std::byte> bytes,
			const std::filesystem::path& path);
		RAVEN_API static std::filesystem::path DefaultContentRoot(bool development);
		RAVEN_API SceneProject(VulkanContext& renderer, std::filesystem::path contentRoot,
			bool watchProject = false, bool watchMeshes = false, bool watchTextures = false);
		RAVEN_API ~SceneProject();
		RAVEN_API const AssetCatalog& GetCatalog() const noexcept;
		RAVEN_API AssetStore& GetAssets() noexcept;
		RAVEN_API const std::filesystem::path& GetContentRoot() const noexcept;
		RAVEN_API std::span<const SceneDescription> GetScenes() const noexcept;
		RAVEN_API std::optional<SceneProjectDefinition> TakeUpdate();
		RAVEN_API bool IsCurrent(const SceneProjectDefinition& candidate) const;
		RAVEN_API void Commit(SceneProjectDefinition& candidate) noexcept;
		RAVEN_API bool IsWatching() const noexcept;

		SceneProject(const SceneProject&) = delete;
		SceneProject& operator=(const SceneProject&) = delete;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

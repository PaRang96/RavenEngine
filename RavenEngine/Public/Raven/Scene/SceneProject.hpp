#pragma once

#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Assets/AssetStore.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Raven
{
	struct SceneDescription
	{
		AssetId Id;
		std::filesystem::path SourcePath;
	};

	// `physics-limits <bodies> <body pairs> <contact constraints>` (docs/GameServicesDesign.md, FD-32):
	// the physics world's fixed capacities, each from 16 to 4194304. Fixed for a session.
	struct PhysicsLimits
	{
		std::uint32_t Bodies = 65536;
		std::uint32_t BodyPairs = 65536;
		std::uint32_t ContactConstraints = 10240;

		bool operator==(const PhysicsLimits&) const = default;
	};

	struct SceneProjectDefinition
	{
		AssetCatalog Catalog;
		std::vector<SceneDescription> Scenes;
		std::vector<std::byte> SourceBytes;
		// `fixed-step <Hz>`: the simulation rate (docs/GameServicesDesign.md, FD-34). Fixed for a
		// session; an edited rate applies at the next start.
		double FixedStepRate = 60.0;
		PhysicsLimits Physics;
	};

	class SceneProject
	{
	public:
		RAVEN_API static SceneProjectDefinition Decode(std::span<const std::byte> bytes,
			const std::filesystem::path& path);
		RAVEN_API SceneProject(VulkanContext& renderer, std::filesystem::path contentRoot,
			bool watchProject = false, bool watchMeshes = false, bool watchTextures = false);
		RAVEN_API ~SceneProject();
		RAVEN_API const AssetCatalog& GetCatalog() const noexcept;
		RAVEN_API AssetStore& GetAssets() noexcept;
		RAVEN_API const std::filesystem::path& GetContentRoot() const noexcept;
		RAVEN_API std::span<const SceneDescription> GetScenes() const noexcept;
		// The rate the project had when the session started.
		RAVEN_API double GetFixedStepRate() const noexcept;
		// The physics limits the project had when the session started.
		RAVEN_API const PhysicsLimits& GetPhysicsLimits() const noexcept;
		RAVEN_API std::optional<SceneProjectDefinition> TakeUpdate();
		RAVEN_API bool IsCurrent(const SceneProjectDefinition& candidate) const;
		RAVEN_API void Commit(SceneProjectDefinition& candidate) noexcept;
		RAVEN_API bool IsWatching() const noexcept;
		// After committing a taken update: records the publish time and returns the reload summary
		// (trigger, import and publish times; docs/HotReloadDesign.md, H7).
		RAVEN_API std::string ReportPublished(std::chrono::duration<double, std::milli> publish);

		SceneProject(const SceneProject&) = delete;
		SceneProject& operator=(const SceneProject&) = delete;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

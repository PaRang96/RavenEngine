#pragma once

#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneProject.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace Raven
{
	// The factory belongs to the engine, never to a replaceable game DLL. It returns a fully
	// prepared candidate scene; F4 cycles through every scene of the project.
	class SceneSession
	{
	public:
		using Factory = std::function<std::unique_ptr<Scene>(std::span<const std::byte>,
			const std::filesystem::path&, const AssetCatalog&)>;
		RAVEN_API SceneSession(SceneProject& project, Factory factory,
			std::optional<AssetId> initialScene = {});
		RAVEN_API ~SceneSession();
		RAVEN_API Scene& GetActive() const noexcept;
		RAVEN_API const SceneDescription& GetActiveDescription() const noexcept;
		// Between gameplay updates. True means a new scene replaced the authored attempt.
		RAVEN_API bool Update(bool restart = false, bool next = false);
		RAVEN_API void Restart();
		// Between gameplay updates: replaces the active world with the project's scene `scene`. False,
		// with one log line and the active world kept, when the project has no such scene or it fails.
		RAVEN_API bool Load(AssetId scene);
		// Replaces the active world with one built from scene bytes, for example a developer
		// quick-save (docs/GameServicesDesign.md, FD-33). The active scene's description, file and
		// watch stay; throws, leaving the active world in place, when the factory rejects the bytes.
		RAVEN_API void RestoreSnapshot(std::span<const std::byte> bytes);

		SceneSession(const SceneSession&) = delete;
		SceneSession& operator=(const SceneSession&) = delete;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

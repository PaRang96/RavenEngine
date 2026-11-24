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

		SceneSession(const SceneSession&) = delete;
		SceneSession& operator=(const SceneSession&) = delete;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

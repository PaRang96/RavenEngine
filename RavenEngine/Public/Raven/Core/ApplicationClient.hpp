#pragma once

#include "Raven/ECS/Entity.hpp"
#include "Raven/Math/Transform.hpp"
#include "Raven/Scene/SceneAccess.hpp"

#include <span>

namespace Raven
{
	class VulkanContext;
	struct FrameTime;
	struct InputState;
	class GameInstance;

	// Null handles leave the corresponding entity unbound.
	struct GameBindings
	{
		ECS::Entity Camera;
		ECS::Entity Player;
		ECS::Entity Mesh;
		SceneAccess World;
		// Closest mesh hit from the presented frame; delivered once on the next update.
		std::span<const ECS::Entity> Interactions;
	};

	struct GameUpdateResult
	{
		Vec3 PlayerDisplacement{};
	};

	// Borrowed only during the synchronous update; no module-owned state survives unload.
	using GameUpdateFunction = GameUpdateResult (*)(GameInstance&, const InputState&, const FrameTime&,
		GameBindings bindings) noexcept;
	using GetGameUpdateFunction = GameUpdateFunction (*)() noexcept;
	using GetGameContractFunction = const char* (*)() noexcept;
	inline constexpr char GameUpdateEntryPoint[] = "RavenGetGameUpdate";
	inline constexpr char GameContractEntryPoint[] = "RavenGetGameContract";

	class ApplicationClient
	{
	public:
		virtual ~ApplicationClient() = default;
		virtual GameBindings GetGameBindings() const noexcept { return {}; }
		virtual bool UsesFixedGameplayStep() const noexcept { return false; }
		// Runs once per drawable frame. A successful world replacement returns true.
		virtual bool PrepareFrame(const InputState&, const FrameTime&) { return false; }
		virtual void OnGameplayStep(const GameUpdateResult&) {}
		virtual void OnFrame(VulkanContext& renderer,
			const InputState& input, const FrameTime& frameTime) = 0;
	};

	// Both callbacks belong to the client module, which creates and destroys its objects.
	struct ApplicationClientFactory
	{
		ApplicationClient* (*Create)(VulkanContext& renderer) = nullptr;
		void (*Destroy)(ApplicationClient* client) noexcept = nullptr;
		ApplicationClient* (*CreateGameClient)(VulkanContext&, GameInstance&) = nullptr;
	};
}

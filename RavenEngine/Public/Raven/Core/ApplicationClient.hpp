#pragma once

namespace Raven
{
	class VulkanContext;
	struct FrameTime;
	struct InputState;
	class GameInstance;

	// Borrowed only during the synchronous update; no module-owned state survives unload.
	using GameUpdateFunction = void (*)(GameInstance&, const InputState&, const FrameTime&) noexcept;
	using GetGameUpdateFunction = GameUpdateFunction (*)() noexcept;
	using GetGameContractFunction = const char* (*)() noexcept;
	inline constexpr char GameUpdateEntryPoint[] = "RavenGetGameUpdate";
	inline constexpr char GameContractEntryPoint[] = "RavenGetGameContract";

	class ApplicationClient
	{
	public:
		virtual ~ApplicationClient() = default;
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

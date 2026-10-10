#pragma once

namespace Raven
{
	class VulkanContext;
	struct FrameTime;
	struct InputState;

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
	};
}

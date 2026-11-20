#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Core/Api.hpp"

#include <memory>

namespace Raven
{
	class AssetCatalog;
	class AssetLease;

	// Main-thread playback. The catalog must outlive the player.
	class AudioPlayer
	{
	public:
		explicit RAVEN_API AudioPlayer(const AssetCatalog& catalog);
		RAVEN_API ~AudioPlayer();

		AudioPlayer(const AudioPlayer&) = delete;
		AudioPlayer& operator=(const AudioPlayer&) = delete;

		// Cache the clip on first use and replace the current one-shot.
		RAVEN_API void Play(AssetRef<AudioAsset> asset);
		RAVEN_API void Play(const AssetLease& asset);
		RAVEN_API void Stop() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

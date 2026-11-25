#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Audio/Sound.hpp"
#include "Raven/Core/Api.hpp"

#include <memory>

namespace Raven
{
	class AssetCatalog;
	class AssetLease;

	// The engine mixer on the main thread (docs/GameServicesDesign.md, C8). The catalog must
	// outlive the player.
	class AudioPlayer
	{
	public:
		explicit RAVEN_API AudioPlayer(const AssetCatalog& catalog);
		RAVEN_API ~AudioPlayer();

		AudioPlayer(const AudioPlayer&) = delete;
		AudioPlayer& operator=(const AudioPlayer&) = delete;

		// Plays a clip, caching it on first use. A bad reference is logged once and returns an
		// empty handle.
		RAVEN_API SoundHandle Play(AssetRef<AudioAsset> asset, const SoundParams& params = {});
		RAVEN_API SoundHandle Play(const AssetLease& asset, const SoundParams& params = {});
		// Fades the sound out; at least about 1 ms, so a stop never clicks.
		RAVEN_API void Stop(SoundHandle sound, float fadeSeconds = 0.0f) noexcept;
		RAVEN_API void StopAll(float fadeSeconds = 0.0f) noexcept;
		RAVEN_API bool IsPlaying(SoundHandle sound) const noexcept;
		RAVEN_API void SetVolume(SoundHandle sound, float volume) noexcept;
		RAVEN_API void SetPitch(SoundHandle sound, float pitch) noexcept;
		RAVEN_API void SetPosition(SoundHandle sound, Vec3 position) noexcept;
		RAVEN_API void SetBusVolume(AudioBus bus, float volume) noexcept;
		RAVEN_API float GetBusVolume(AudioBus bus) const noexcept;
		// Where spatial sounds are heard from, with the listener's right direction.
		RAVEN_API void SetListener(Vec3 position, Vec3 right) noexcept;
		RAVEN_API void ClearListener() noexcept;
		// Once per frame; parameter changes and virtual voices take effect here.
		RAVEN_API void Update() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

#include "Audio/AudioPlayer.hpp"

#include "Assets/ManagedAsset.hpp"
#include "Audio/AudioClip.hpp"
#include "Audio/AudioOutput.hpp"
#include "Audio/AudioVoices.hpp"
#include "Raven/Assets/AssetStore.hpp"
#include "Raven/Core/Log.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	struct AudioPlayer::Impl
	{
		~Impl()
		{
			// Stop the audio thread before the voices release the clips it reads.
			Output.reset();
		}

		// Opens the device in the background when the session starts, so the first sound causes no
		// frame hitch.
		std::unique_ptr<AudioOutput> Output = std::make_unique<AudioOutput>();
		AudioVoices Voices{ Output->GetMixer() };
		std::chrono::steady_clock::time_point LastUpdate = std::chrono::steady_clock::now();
	};

	AudioPlayer::AudioPlayer()
		: m_Impl(std::make_unique<Impl>())
	{
	}

	AudioPlayer::~AudioPlayer() = default;

	SoundHandle AudioPlayer::Play(const AssetLease& asset, const SoundParams& params)
	{
		try
		{
			if (!asset.m_Asset || !asset.m_Asset->Clip)
				throw std::invalid_argument("asset does not refer to a loaded audio clip");
			return m_Impl->Voices.Play(asset.m_Asset->Clip, params);
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Warning, "Cannot play scene audio: " + std::string(error.what()));
			return {};
		}
	}

	void AudioPlayer::Stop(SoundHandle sound, float fadeSeconds) noexcept
	{
		m_Impl->Voices.Stop(sound, fadeSeconds);
	}

	void AudioPlayer::StopAll(float fadeSeconds) noexcept
	{
		m_Impl->Voices.StopAll(fadeSeconds);
	}

	bool AudioPlayer::IsPlaying(SoundHandle sound) const noexcept
	{
		return m_Impl->Voices.IsPlaying(sound);
	}

	void AudioPlayer::SetVolume(SoundHandle sound, float volume) noexcept
	{
		m_Impl->Voices.SetVolume(sound, volume);
	}

	void AudioPlayer::SetPitch(SoundHandle sound, float pitch) noexcept
	{
		m_Impl->Voices.SetPitch(sound, pitch);
	}

	void AudioPlayer::SetPosition(SoundHandle sound, Vec3 position) noexcept
	{
		m_Impl->Voices.SetPosition(sound, position);
	}

	void AudioPlayer::SetBusVolume(AudioBus bus, float volume) noexcept
	{
		m_Impl->Voices.SetBusVolume(bus, volume);
	}

	float AudioPlayer::GetBusVolume(AudioBus bus) const noexcept
	{
		return m_Impl->Voices.GetBusVolume(bus);
	}

	void AudioPlayer::SetListener(Vec3 position, Vec3 right) noexcept
	{
		m_Impl->Voices.SetListener(position, right);
	}

	void AudioPlayer::ClearListener() noexcept
	{
		m_Impl->Voices.ClearListener();
	}

	void AudioPlayer::Update() noexcept
	{
		const auto now = std::chrono::steady_clock::now();
		const std::chrono::duration<double> elapsed = now - m_Impl->LastUpdate;
		m_Impl->LastUpdate = now;
		m_Impl->Voices.Update(elapsed.count());
	}
}

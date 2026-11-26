#pragma once

#include "Audio/AudioClip.hpp"
#include "Audio/AudioMixer.hpp"
#include "Raven/Audio/Sound.hpp"
#include "Raven/Core/Api.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Raven
{
	// The engine mixer's control half (docs/GameServicesDesign.md, C8), on the main thread. It owns
	// every playing sound and decides, once per frame, which of them the audio thread mixes:
	// - concurrency groups cap sounds that share a group, with a rule for the next one;
	// - sounds are ranked by priority times how loud they are heard (volume, distance, bus);
	// - the loudest MaxVoices are real. The rest are virtual: their time keeps running, loops
	//   resume where they would be, and a one-shot that loses its voice stops;
	// - steals, virtualization and resumes fade over 10 ms, so they never click;
	// - Dialogue ducks Music and Ambience.
	// Handles are serials that never repeat within a run, starting at a random value.
	//
	// Clip lifetime follows the audio thread, not the sound: each voice slot owns its clip from the
	// Start until the mixer reports the slot released. A sound may therefore be erased (stopped,
	// stolen, or left behind by a scene change) while its voice still fades. A Stop the command queue
	// drops is retried per slot until it is sent, so no voice keeps looping without an owner.
	class AudioVoices
	{
	public:
		static constexpr std::size_t MaxSounds = 512;

		RAVEN_API explicit AudioVoices(AudioMixer& mixer);
		AudioVoices(const AudioVoices&) = delete;
		AudioVoices& operator=(const AudioVoices&) = delete;

		// An empty handle when the clip is empty, the group refuses it or too many sounds play.
		RAVEN_API SoundHandle Play(std::shared_ptr<const PcmClip> clip, const SoundParams& params);
		void Stop(SoundHandle sound, float fadeSeconds) noexcept;
		RAVEN_API void StopAll(float fadeSeconds) noexcept;
		bool IsPlaying(SoundHandle sound) const noexcept;
		RAVEN_API void SetVolume(SoundHandle sound, float volume) noexcept;
		void SetPitch(SoundHandle sound, float pitch) noexcept;
		void SetPosition(SoundHandle sound, Vec3 position) noexcept;
		void SetBusVolume(AudioBus bus, float volume) noexcept;
		float GetBusVolume(AudioBus bus) const noexcept;
		// Spatial sounds are heard from here; without a listener they play unattenuated and centered.
		void SetListener(Vec3 position, Vec3 right) noexcept;
		void ClearListener() noexcept;

		// Once per frame: collects finished voices, advances virtual time, updates gains and buses,
		// and moves sounds between real and virtual.
		RAVEN_API void Update(double seconds) noexcept;

		struct Counts
		{
			std::size_t Real = 0;
			std::size_t Virtual = 0;
			std::size_t Stopping = 0;
		};
		RAVEN_API Counts GetCounts() const noexcept;

	private:
		enum class State : std::uint8_t { Virtual, Real, Stopping };

		struct Sound
		{
			std::uint64_t Serial = 0;
			std::shared_ptr<const PcmClip> Clip;
			SoundParams Params;
			std::uint64_t Order = 0;
			State Mode = State::Virtual;
			bool Started = false;
			// The audio voice while real or stopping.
			std::uint32_t Slot = 0;
			std::uint64_t Voice = 0;
			// Frames; tracked here while virtual and read back from the mixer while real.
			double Position = 0.0;
			float Left = 0.0f;
			float Right = 0.0f;
			float Rate = 1.0f;
			// What the audio voice was last told.
			float SentLeft = 0.0f;
			float SentRight = 0.0f;
			float SentRate = 1.0f;
			// Volume after distance and buses; Score adds priority.
			float Audible = 0.0f;
			float Score = 0.0f;
		};

		Sound* Find(SoundHandle sound) noexcept;
		const Sound* Find(SoundHandle sound) const noexcept;
		void Measure(Sound& sound) const noexcept;
		bool StartVoice(Sound& sound, std::uint32_t rampFrames) noexcept;
		// Real sounds fade out; a looping one becomes virtual when `keep` is set.
		void Release(Sound& sound, std::uint32_t fadeFrames, bool keep) noexcept;
		bool Send(const AudioMixer::Command& command) noexcept;
		void Collect() noexcept;
		void RetryStops() noexcept;
		void EraseStopped() noexcept;
		void UpdateBuses(double seconds) noexcept;
		float BusGain(AudioBus bus) const noexcept;
		std::optional<std::uint32_t> FreeSlot() const noexcept;
		std::size_t CountReal() const noexcept;

		AudioMixer& m_Mixer;
		std::vector<Sound> m_Sounds;
		// The audio voice each slot runs, or zero once its release arrived.
		std::array<std::uint64_t, AudioMixer::MaxVoices> m_Slots{};
		// The clip each slot's voice reads, held until that release arrives.
		std::array<std::shared_ptr<const PcmClip>, AudioMixer::MaxVoices> m_SlotClips{};
		// A Stop the command queue dropped, per slot, resent each Update until it goes through.
		struct PendingStop
		{
			std::uint64_t Voice = 0;
			std::uint32_t RampFrames = 0;
		};
		std::array<PendingStop, AudioMixer::MaxVoices> m_PendingStops{};
		std::uint64_t m_NextSerial = 0;
		std::uint64_t m_NextVoice = 1;
		std::uint64_t m_NextOrder = 0;
		std::array<float, AudioBusCount> m_BusVolume{};
		std::array<float, AudioBusCount> m_SentBusGain{};
		float m_Duck = 1.0f;
		bool m_HasListener = false;
		Vec3 m_ListenerPosition{};
		Vec3 m_ListenerRight{ 1.0f, 0.0f, 0.0f };
		bool m_ReportedFull = false;
		bool m_ReportedQueue = false;
	};
}

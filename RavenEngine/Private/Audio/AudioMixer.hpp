#pragma once

#include "Audio/AudioDevice.hpp"
#include "Raven/Audio/Sound.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Raven
{
	// A fixed-capacity queue for one producer thread and one consumer thread, without locks or
	// allocation after construction. Capacity must be a power of two.
	template <typename T, std::size_t Capacity>
	class AudioQueue
	{
		static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0);

	public:
		bool TryPush(const T& value) noexcept
		{
			const std::size_t tail = m_Tail.load(std::memory_order_relaxed);
			if (tail - m_Head.load(std::memory_order_acquire) == Capacity)
				return false;
			m_Items[tail % Capacity] = value;
			m_Tail.store(tail + 1, std::memory_order_release);
			return true;
		}

		bool TryPop(T& value) noexcept
		{
			const std::size_t head = m_Head.load(std::memory_order_relaxed);
			if (head == m_Tail.load(std::memory_order_acquire))
				return false;
			value = m_Items[head % Capacity];
			m_Head.store(head + 1, std::memory_order_release);
			return true;
		}

		std::size_t FreeSpace() const noexcept
		{
			return Capacity - (m_Tail.load(std::memory_order_relaxed) - m_Head.load(std::memory_order_acquire));
		}

	private:
		std::array<T, Capacity> m_Items{};
		alignas(64) std::atomic<std::size_t> m_Head{ 0 };
		alignas(64) std::atomic<std::size_t> m_Tail{ 0 };
	};

	// The engine mixer's real-time half (docs/GameServicesDesign.md, C8), with no device or thread of
	// its own. The main thread's voice manager (AudioVoices) decides what plays and sends commands;
	// the audio thread renders fixed 256-frame blocks in the mixer format.
	//
	// Each voice reads a clip the main thread keeps alive, with a fractional position for pitch, a
	// stereo gain ramp for fades and parameter changes, and a bus. Buses are mixed into their own
	// buffers so per-bus effect slots can be added without changing voices. When a voice ends or a
	// stop fade finishes, the audio thread reports its slot back, and only then may the main thread
	// free the clip or reuse the slot. The audio thread never allocates, locks or touches assets.
	class AudioMixer
	{
	public:
		static constexpr std::uint32_t BlockFrames = 256;
		static constexpr std::uint32_t MaxVoices = 64;
		static constexpr std::size_t CommandCapacity = 1024;

		enum class CommandType : std::uint8_t { Start, Update, Stop, BusGain };

		struct Command
		{
			CommandType Type = CommandType::Start;
			std::uint8_t Bus = 0;
			bool Loop = false;
			std::uint32_t Slot = 0;
			std::uint64_t Serial = 0;
			// Start only: interleaved mixer-format samples the main thread keeps alive until released.
			const float* Samples = nullptr;
			std::uint32_t Frames = 0;
			std::uint32_t StartFrame = 0;
			// Start and Update: playback rate and target gains. BusGain uses GainLeft.
			float Rate = 1.0f;
			float GainLeft = 0.0f;
			float GainRight = 0.0f;
			// The ramp to the target gains (Start: from silence; Stop: to silence). Zero is immediate.
			std::uint32_t RampFrames = 0;
		};

		struct Release
		{
			std::uint32_t Slot = 0;
			std::uint64_t Serial = 0;
		};

		AudioMixer() = default;
		AudioMixer(const AudioMixer&) = delete;
		AudioMixer& operator=(const AudioMixer&) = delete;

		// Main thread.
		bool Send(const Command& command) noexcept { return m_Commands.TryPush(command); }
		std::size_t CommandSpace() const noexcept { return m_Commands.FreeSpace(); }
		bool TakeRelease(Release& release) noexcept { return m_Released.TryPop(release); }
		// The whole-frame position a slot last reached, published once per block.
		std::uint32_t GetPosition(std::uint32_t slot) const noexcept
		{
			return m_Positions[slot].load(std::memory_order_relaxed);
		}

		// Audio thread: fills frames * MixerChannels samples, for any frame count.
		void Render(float* out, std::uint32_t frames) noexcept;
		static void RenderCallback(void* mixer, float* out, std::uint32_t frames) noexcept;

	private:
		struct Voice
		{
			const float* Samples = nullptr;
			std::uint32_t Frames = 0;
			bool Loop = false;
			bool StopAfterRamp = false;
			std::uint8_t Bus = 0;
			std::uint64_t Serial = 0;
			// Position in frames as 32.32 fixed point, and the step per output frame.
			std::uint64_t Position = 0;
			std::uint64_t Step = 0;
			float GainLeft = 0.0f;
			float GainRight = 0.0f;
			float TargetLeft = 0.0f;
			float TargetRight = 0.0f;
			std::uint32_t RampLeft = 0;
		};

		struct Bus
		{
			float Gain = 1.0f;
			float Target = 1.0f;
		};

		using Block = std::array<float, std::size_t{ BlockFrames } * MixerChannels>;

		void Apply(const Command& command) noexcept;
		void MixBlock() noexcept;
		// Returns false when the voice finished during this block.
		bool MixVoice(Voice& voice, float* out) noexcept;
		void Finish(std::uint32_t slot) noexcept;

		AudioQueue<Command, CommandCapacity> m_Commands;
		// One outstanding release per slot at most, so this never fills.
		AudioQueue<Release, MaxVoices * 2> m_Released;
		std::array<std::atomic<std::uint32_t>, MaxVoices> m_Positions{};

		// Audio thread only.
		std::array<Voice, MaxVoices> m_Voices{};
		std::array<Bus, AudioBusCount> m_Buses{};
		std::array<Block, AudioBusCount> m_BusBlocks{};
		Block m_Block{};
		std::uint32_t m_BlockRead = BlockFrames;
	};
}

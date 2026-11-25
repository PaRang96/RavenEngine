#include "Audio/AudioMixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Raven
{
	namespace
	{
		constexpr std::uint64_t FrameOne = std::uint64_t{ 1 } << 32;
	}

	void AudioMixer::Render(float* out, std::uint32_t frames) noexcept
	{
		while (frames > 0)
		{
			if (m_BlockRead == BlockFrames)
			{
				MixBlock();
				m_BlockRead = 0;
			}
			const std::uint32_t count = std::min(frames, BlockFrames - m_BlockRead);
			std::memcpy(out, m_Block.data() + std::size_t{ m_BlockRead } * MixerChannels,
				std::size_t{ count } * MixerChannels * sizeof(float));
			out += std::size_t{ count } * MixerChannels;
			frames -= count;
			m_BlockRead += count;
		}
	}

	void AudioMixer::RenderCallback(void* mixer, float* out, std::uint32_t frames) noexcept
	{
		static_cast<AudioMixer*>(mixer)->Render(out, frames);
	}

	void AudioMixer::Apply(const Command& command) noexcept
	{
		if (command.Type == CommandType::BusGain)
		{
			if (command.Bus < AudioBusCount)
				m_Buses[command.Bus].Target = command.GainLeft;
			return;
		}
		if (command.Slot >= MaxVoices)
			return;
		Voice& voice = m_Voices[command.Slot];
		if (command.Type == CommandType::Start)
		{
			// The voice manager reuses a slot only after its release, so this is a defensive reset.
			if (voice.Samples)
				Finish(command.Slot);
			if (!command.Samples || command.Frames == 0 || command.Bus >= AudioBusCount ||
				(command.StartFrame >= command.Frames && !command.Loop))
			{
				m_Released.TryPush({ command.Slot, command.Serial });
				return;
			}
			voice = {};
			voice.Samples = command.Samples;
			voice.Frames = command.Frames;
			voice.Loop = command.Loop;
			voice.Bus = command.Bus;
			voice.Serial = command.Serial;
			voice.Position = std::uint64_t{ command.StartFrame % command.Frames } << 32;
			voice.Step = static_cast<std::uint64_t>(std::llround(static_cast<double>(command.Rate) * FrameOne));
			voice.TargetLeft = command.GainLeft;
			voice.TargetRight = command.GainRight;
			voice.RampLeft = command.RampFrames;
			if (command.RampFrames == 0)
			{
				voice.GainLeft = voice.TargetLeft;
				voice.GainRight = voice.TargetRight;
			}
			m_Positions[command.Slot].store(command.StartFrame % command.Frames, std::memory_order_relaxed);
			return;
		}
		// Commands for a voice that already ended carry an older serial and are ignored.
		if (!voice.Samples || voice.Serial != command.Serial || voice.StopAfterRamp)
			return;
		if (command.Type == CommandType::Update)
		{
			voice.Step = static_cast<std::uint64_t>(std::llround(static_cast<double>(command.Rate) * FrameOne));
			voice.TargetLeft = command.GainLeft;
			voice.TargetRight = command.GainRight;
			voice.RampLeft = command.RampFrames;
			if (command.RampFrames == 0)
			{
				voice.GainLeft = voice.TargetLeft;
				voice.GainRight = voice.TargetRight;
			}
		}
		else if (command.RampFrames == 0)
			Finish(command.Slot);
		else
		{
			voice.TargetLeft = voice.TargetRight = 0.0f;
			voice.RampLeft = command.RampFrames;
			voice.StopAfterRamp = true;
		}
	}

	void AudioMixer::MixBlock() noexcept
	{
		// Commands take effect at block boundaries (5.3 ms at 48 kHz).
		Command command;
		while (m_Commands.TryPop(command))
			Apply(command);

		for (auto& block : m_BusBlocks)
			block.fill(0.0f);
		for (std::uint32_t slot = 0; slot < MaxVoices; ++slot)
		{
			Voice& voice = m_Voices[slot];
			if (!voice.Samples)
				continue;
			if (MixVoice(voice, m_BusBlocks[voice.Bus].data()))
				m_Positions[slot].store(static_cast<std::uint32_t>(voice.Position >> 32), std::memory_order_relaxed);
			else
				Finish(slot);
		}

		// Every bus feeds Master (bus 0). Gains ramp across one block, so volume changes never click.
		const auto applyGain = [](Bus& bus, const float* in, float* out)
		{
			const float step = (bus.Target - bus.Gain) / static_cast<float>(BlockFrames);
			float gain = bus.Gain;
			for (std::uint32_t frame = 0; frame < BlockFrames; ++frame, gain += step)
			{
				out[frame * 2] += in[frame * 2] * gain;
				out[frame * 2 + 1] += in[frame * 2 + 1] * gain;
			}
			bus.Gain = bus.Target;
		};
		for (std::size_t bus = 1; bus < AudioBusCount; ++bus)
			applyGain(m_Buses[bus], m_BusBlocks[bus].data(), m_BusBlocks[0].data());
		m_Block.fill(0.0f);
		applyGain(m_Buses[0], m_BusBlocks[0].data(), m_Block.data());
		// A safety clamp, not a limiter: the device never receives samples outside the float range.
		for (float& sample : m_Block)
			sample = std::clamp(sample, -1.0f, 1.0f);
	}

	bool AudioMixer::MixVoice(Voice& voice, float* out) noexcept
	{
		for (std::uint32_t frame = 0; frame < BlockFrames; ++frame)
		{
			std::uint64_t index = voice.Position >> 32;
			if (index >= voice.Frames)
			{
				if (!voice.Loop)
					return false;
				voice.Position -= (index / voice.Frames) * voice.Frames * FrameOne;
				index = voice.Position >> 32;
			}
			if (voice.RampLeft > 0)
			{
				voice.GainLeft += (voice.TargetLeft - voice.GainLeft) / static_cast<float>(voice.RampLeft);
				voice.GainRight += (voice.TargetRight - voice.GainRight) / static_cast<float>(voice.RampLeft);
				if (--voice.RampLeft == 0 && voice.StopAfterRamp)
					return false;
			}

			const float* current = voice.Samples + index * MixerChannels;
			float left = current[0];
			float right = current[1];
			if (const std::uint32_t fraction = static_cast<std::uint32_t>(voice.Position); fraction != 0)
			{
				// Linear interpolation for pitched playback; the clip end holds its last frame, and a
				// loop interpolates into its first.
				const std::uint64_t nextIndex = index + 1 < voice.Frames ? index + 1 : (voice.Loop ? 0 : index);
				const float* next = voice.Samples + nextIndex * MixerChannels;
				const float weight = static_cast<float>(fraction) * (1.0f / 4294967296.0f);
				left += (next[0] - left) * weight;
				right += (next[1] - right) * weight;
			}
			out[frame * 2] += left * voice.GainLeft;
			out[frame * 2 + 1] += right * voice.GainRight;
			voice.Position += voice.Step;
		}
		return voice.Loop || (voice.Position >> 32) < voice.Frames;
	}

	void AudioMixer::Finish(std::uint32_t slot) noexcept
	{
		Voice& voice = m_Voices[slot];
		// Room is guaranteed: each slot has at most one release outstanding.
		static_cast<void>(m_Released.TryPush({ slot, voice.Serial }));
		voice = {};
	}
}

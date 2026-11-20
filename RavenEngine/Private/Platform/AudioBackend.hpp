#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{
	struct PcmClip
	{
		std::uint16_t Channels = 0;
		std::uint32_t SampleRate = 0;
		std::vector<std::uint8_t> Samples;
	};

	class AudioBackend
	{
	public:
		virtual ~AudioBackend() = default;

		// Samples must stay alive until Stop() returns or the backend is destroyed.
		virtual void Play(const PcmClip& clip) = 0;
		virtual void Stop() noexcept = 0;
	};

	std::unique_ptr<AudioBackend> CreateAudioBackend();
}

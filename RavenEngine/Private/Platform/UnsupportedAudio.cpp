#include "Platform/AudioBackend.hpp"

#include <stdexcept>

namespace Raven
{
	std::unique_ptr<AudioBackend> CreateAudioBackend()
	{
		throw std::runtime_error("audio playback is unavailable on this platform");
	}
}

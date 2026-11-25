#pragma once

#include "Audio/AudioMixer.hpp"

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

namespace Raven
{
	// Owns the mixer and the device lifecycle (docs/UIAudioLibraries.md, "Is miniaudio enough?",
	// condition 3). A control thread opens the default device and reopens it when it is lost. With
	// no device, the null device keeps the mixer running silently while the control thread retries.
	class AudioOutput
	{
	public:
		// Returns at once; the device opens on the control thread (about 120 ms for WASAPI), and
		// sounds sent before that start when it opens.
		AudioOutput();
		~AudioOutput();
		AudioOutput(const AudioOutput&) = delete;
		AudioOutput& operator=(const AudioOutput&) = delete;

		AudioMixer& GetMixer() noexcept { return m_Mixer; }

	private:
		void Run(std::stop_token stop);

		// Declared first: destroyed after the control thread has closed the device.
		AudioMixer m_Mixer;
		std::mutex m_Mutex;
		std::condition_variable_any m_Wake;
		std::jthread m_Control;
	};
}

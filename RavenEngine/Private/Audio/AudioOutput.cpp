#include "Audio/AudioOutput.hpp"

#include "Raven/Core/Log.hpp"

#include <chrono>
#include <exception>
#include <format>

namespace Raven
{
	namespace
	{
		constexpr auto PollInterval = std::chrono::milliseconds(100);
		constexpr auto RetryInterval = std::chrono::seconds(2);
		constexpr auto LostRetryDelay = std::chrono::milliseconds(250);
	}

	AudioOutput::AudioOutput()
		: m_Control([this](std::stop_token stop) { Run(stop); })
	{
	}

	AudioOutput::~AudioOutput()
	{
		m_Control.request_stop();
		m_Wake.notify_all();
		m_Control.join();
	}

	void AudioOutput::Run(std::stop_token stop)
	{
		const AudioDeviceRequest request{ &AudioMixer::RenderCallback, &m_Mixer };
		std::unique_ptr<AudioDevice> device;
		bool silent = false;
		bool failureReported = false;
		auto nextAttempt = std::chrono::steady_clock::now();
		const auto openSilent = [&]
		{
			device = CreateNullAudioDevice();
			device->Open(request);
			silent = true;
		};

		while (!stop.stop_requested())
		{
			if ((!device || silent) && std::chrono::steady_clock::now() >= nextAttempt)
			{
				// Only one device renders the mixer at a time: close the silent one first.
				if (device)
					device->Close();
				device.reset();
				try
				{
					auto opened = CreateAudioDevice();
					const std::string description = opened->Open(request);
					device = std::move(opened);
					silent = false;
					failureReported = false;
					Log(LogLevel::Info, std::format("Audio output: {}", description));
				}
				catch (const std::exception& error)
				{
					if (!failureReported)
						Log(LogLevel::Warning, std::format("No audio output ({}). Rendering silence and retrying every {} s",
							error.what(), RetryInterval.count()));
					failureReported = true;
					openSilent();
					nextAttempt = std::chrono::steady_clock::now() + RetryInterval;
				}
			}

			const AudioDeviceEvents events = device->TakeEvents();
			if (HasEvent(events, AudioDeviceEvents::Moved))
				Log(LogLevel::Info, "Audio output moved to the new default device");
			if (HasEvent(events, AudioDeviceEvents::Lost))
			{
				Log(LogLevel::Warning, "Audio device lost; reopening the default output");
				device->Close();
				// Stay silent briefly, so a device that keeps failing cannot spin this loop.
				openSilent();
				nextAttempt = std::chrono::steady_clock::now() + LostRetryDelay;
			}

			std::unique_lock lock(m_Mutex);
			m_Wake.wait_for(lock, stop, PollInterval, [] { return false; });
		}
		if (device)
			device->Close();
	}
}

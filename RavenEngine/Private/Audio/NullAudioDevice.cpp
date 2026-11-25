#include "Audio/AudioDevice.hpp"

#include <chrono>
#include <thread>
#include <vector>

namespace Raven
{
	namespace
	{
		// Renders 10 ms at a time on its own thread, paced by the clock, so the mixer advances and
		// releases clips as it would on a device.
		class NullAudioDevice final : public AudioDevice
		{
		public:
			~NullAudioDevice() override { Close(); }

			std::string Open(const AudioDeviceRequest& request) override
			{
				Close();
				m_Thread = std::jthread([request](std::stop_token stop)
				{
					constexpr std::uint32_t frames = MixerSampleRate / 100;
					constexpr auto period = std::chrono::milliseconds(10);
					std::vector<float> buffer(std::size_t{ frames } * MixerChannels);
					auto next = std::chrono::steady_clock::now();
					while (!stop.stop_requested())
					{
						request.Render(request.User, buffer.data(), frames);
						next += period;
						const auto now = std::chrono::steady_clock::now();
						// After a long stall, continue from now instead of rendering the backlog at once.
						if (next < now - 10 * period)
							next = now;
						std::this_thread::sleep_until(next);
					}
				});
				return "null output (silent)";
			}

			void Close() noexcept override
			{
				if (m_Thread.joinable())
				{
					m_Thread.request_stop();
					m_Thread.join();
				}
			}

			AudioDeviceEvents TakeEvents() noexcept override { return AudioDeviceEvents::None; }

		private:
			std::jthread m_Thread;
		};
	}

	std::unique_ptr<AudioDevice> CreateNullAudioDevice()
	{
		return std::make_unique<NullAudioDevice>();
	}
}

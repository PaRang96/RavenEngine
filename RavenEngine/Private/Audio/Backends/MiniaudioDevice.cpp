// The miniaudio device backend (RAVEN_AUDIO_BACKEND=miniaudio). Every miniaudio device call in the
// engine is in this file; swapping the backend replaces this file only.

#include "Audio/AudioDevice.hpp"

#include <miniaudio.h>

#include <atomic>
#include <format>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		std::runtime_error AudioError(const char* operation, ma_result result)
		{
			return std::runtime_error(std::format("{} failed: {}", operation, ma_result_description(result)));
		}

		class MiniaudioDevice final : public AudioDevice
		{
		public:
			~MiniaudioDevice() override { Close(); }

			std::string Open(const AudioDeviceRequest& request) override
			{
				Close();
				m_Request = request;
				m_Events.store(0, std::memory_order_relaxed);

				// miniaudio tries the backends compiled in (cmake/RavenAudio.cmake) in its own order.
				const ma_context_config contextConfig = ma_context_config_init();
				if (const ma_result result = ma_context_init(nullptr, 0, &contextConfig, &m_Context); result != MA_SUCCESS)
					throw AudioError("Audio context initialization", result);
				m_HasContext = true;

				ma_device_config config = ma_device_config_init(ma_device_type_playback);
				config.playback.format = ma_format_f32;
				config.playback.channels = MixerChannels;
				config.sampleRate = MixerSampleRate;
				config.periodSizeInFrames = request.PreferredFrames;
				config.performanceProfile = ma_performance_profile_low_latency;
				// The mixer renders any frame count, so miniaudio needs no extra buffer of its own.
				config.noFixedSizedCallback = MA_TRUE;
				config.dataCallback = &MiniaudioDevice::Render;
				config.notificationCallback = &MiniaudioDevice::Notify;
				config.pUserData = this;
				config.wasapi.usage = ma_wasapi_usage_games;
				// Default-device moves stay on: WASAPI and Core Audio follow the system default.

				m_Closing.store(true, std::memory_order_relaxed);
				if (const ma_result result = ma_device_init(&m_Context, &config, &m_Device); result != MA_SUCCESS)
				{
					Close();
					throw AudioError("Audio device initialization", result);
				}
				m_HasDevice = true;
				m_Closing.store(false, std::memory_order_release);
				if (const ma_result result = ma_device_start(&m_Device); result != MA_SUCCESS)
				{
					Close();
					throw AudioError("Audio device start", result);
				}

				char name[256]{};
				ma_device_get_name(&m_Device, ma_device_type_playback, name, sizeof(name), nullptr);
				return std::format("{} '{}', {} Hz, {} channels, {}-frame period",
					ma_get_backend_name(m_Context.backend), name, m_Device.playback.internalSampleRate,
					m_Device.playback.internalChannels, m_Device.playback.internalPeriodSizeInFrames);
			}

			void Close() noexcept override
			{
				// Stopping posts a "stopped" notification, which is not a lost device.
				m_Closing.store(true, std::memory_order_release);
				if (m_HasDevice)
				{
					// Waits for the audio thread: no Render call runs after this returns.
					ma_device_uninit(&m_Device);
					m_HasDevice = false;
				}
				if (m_HasContext)
				{
					ma_context_uninit(&m_Context);
					m_HasContext = false;
				}
			}

			AudioDeviceEvents TakeEvents() noexcept override
			{
				return static_cast<AudioDeviceEvents>(m_Events.exchange(0, std::memory_order_acq_rel));
			}

		private:
			static void Render(ma_device* device, void* output, const void*, ma_uint32 frames)
			{
				const auto& request = static_cast<MiniaudioDevice*>(device->pUserData)->m_Request;
				request.Render(request.User, static_cast<float*>(output), frames);
			}

			// May run on the audio thread: it only sets flags. Starting or stopping a device from here
			// is not allowed (miniaudio #777); the control thread reopens it.
			static void Notify(const ma_device_notification* notification)
			{
				auto* self = static_cast<MiniaudioDevice*>(notification->pDevice->pUserData);
				if (notification->type == ma_device_notification_type_stopped &&
					!self->m_Closing.load(std::memory_order_acquire))
					self->Post(AudioDeviceEvents::Lost);
				else if (notification->type == ma_device_notification_type_rerouted)
					self->Post(AudioDeviceEvents::Moved);
			}

			void Post(AudioDeviceEvents event) noexcept
			{
				m_Events.fetch_or(static_cast<std::uint32_t>(event), std::memory_order_acq_rel);
			}

			AudioDeviceRequest m_Request;
			ma_context m_Context{};
			ma_device m_Device{};
			bool m_HasContext = false;
			bool m_HasDevice = false;
			std::atomic<bool> m_Closing{ true };
			std::atomic<std::uint32_t> m_Events{ 0 };
		};
	}

	std::unique_ptr<AudioDevice> CreateAudioDevice()
	{
		return std::make_unique<MiniaudioDevice>();
	}
}

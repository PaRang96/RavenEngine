#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Raven
{
	// The engine's only view of an audio output (docs/UIAudioLibraries.md, "Hiding the device layer").
	// RAVEN_AUDIO_BACKEND selects the one file under Audio/Backends/ that defines CreateAudioDevice;
	// no other engine file names the library behind it.

	// The mixer's fixed format: interleaved float32 stereo at 48 kHz. Backends convert it to the
	// device's own format, so reopening a device at another rate never disturbs the mixer.
	inline constexpr std::uint32_t MixerSampleRate = 48000;
	inline constexpr std::uint32_t MixerChannels = 2;

	// Called on the backend's audio thread, for any frame count. It must not lock, allocate or throw.
	using AudioRenderFn = void (*)(void* user, float* out, std::uint32_t frames) noexcept;

	// Posted by the backend from any thread without locking, and taken by the control thread.
	enum class AudioDeviceEvents : std::uint32_t
	{
		None = 0,
		Lost = 1u << 0,  // the device stopped without a request; close and reopen it
		Moved = 1u << 1, // the backend followed a new system default by itself (logged only)
	};

	constexpr AudioDeviceEvents operator|(AudioDeviceEvents left, AudioDeviceEvents right) noexcept
	{
		return static_cast<AudioDeviceEvents>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
	}

	constexpr bool HasEvent(AudioDeviceEvents events, AudioDeviceEvents event) noexcept
	{
		return (static_cast<std::uint32_t>(events) & static_cast<std::uint32_t>(event)) != 0;
	}

	struct AudioDeviceRequest
	{
		AudioRenderFn Render = nullptr;
		void* User = nullptr;
		// A hint only; 0 lets the backend choose. Nothing above the device may rely on it.
		std::uint32_t PreferredFrames = 0;
	};

	// One output on the system default device. Open, Close and TakeEvents are called from one
	// control thread; only Render runs on the audio thread.
	class AudioDevice
	{
	public:
		virtual ~AudioDevice() = default;

		// Opens the default output and starts rendering. Returns a description for the log;
		// throws std::runtime_error when no output can be opened.
		virtual std::string Open(const AudioDeviceRequest& request) = 0;
		// Stops rendering. No Render call runs after Close returns; closing twice is harmless.
		virtual void Close() noexcept = 0;
		// Returns and clears the events posted since the last call.
		virtual AudioDeviceEvents TakeEvents() noexcept = 0;
	};

	// The backend selected by RAVEN_AUDIO_BACKEND (cmake/RavenAudio.cmake).
	std::unique_ptr<AudioDevice> CreateAudioDevice();
	// Calls Render in real time and discards the output: the fallback when no device exists, and
	// headless runs. It never fails to open.
	std::unique_ptr<AudioDevice> CreateNullAudioDevice();
}

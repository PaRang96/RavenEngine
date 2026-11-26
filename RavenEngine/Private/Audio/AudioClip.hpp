#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Raven
{
	// A decoded clip in the mixer format (Audio/AudioDevice.hpp): interleaved float32 stereo at
	// MixerSampleRate, converted once at load so the audio thread only reads and mixes.
	struct PcmClip
	{
		std::vector<float> Samples;
		std::uint32_t Frames = 0;
	};

	// Encoded files larger than this are refused before decoding.
	inline constexpr std::size_t MaxAudioFileBytes = 64 * 1024 * 1024;
	// Clips are decoded whole, 384 KB per second; longer sounds wait for streaming.
	inline constexpr std::uint32_t MaxClipSeconds = 300;
	// Every decoder bounds its output before allocating (docs/AssetSystem.md, content trust): a
	// decoded buffer, in the source's channels and rate, never grows past this. A 300-second stereo
	// clip at 48 kHz is about 115 MB.
	inline constexpr std::size_t MaxDecodedAudioBytes = 128 * 1024 * 1024;

	// True for the extensions DecodeAudio reads: .wav, .flac, .mp3 and .ogg, in any letter case.
	bool IsAudioExtension(const std::filesystem::path& path);
	// Decodes WAV, FLAC, MP3 or Ogg Vorbis (chosen by the file extension) and converts it to the mixer format.
	PcmClip DecodeAudio(std::span<const std::byte> bytes, const std::filesystem::path& path);
}

// Clip decoding behind PcmClip: the second place miniaudio is used (ThirdParty/miniaudio/README.md) and the
// only place stb_vorbis is (ThirdParty/stb_vorbis/README.md). miniaudio's WAV, FLAC and MP3 decoders and
// stb_vorbis read the file; miniaudio's converter resamples to the mixer format.

#include "Audio/AudioClip.hpp"
#include "Audio/AudioDevice.hpp"
#include "Core/PathText.hpp"

#include <miniaudio.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		ma_encoding_format FormatOf(const std::filesystem::path& path)
		{
			auto extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			if (extension == ".wav")
				return ma_encoding_format_wav;
			if (extension == ".flac")
				return ma_encoding_format_flac;
			if (extension == ".mp3")
				return ma_encoding_format_mp3;
			if (extension == ".ogg")
				return ma_encoding_format_vorbis;
			return ma_encoding_format_unknown;
		}

		// Converts decoded interleaved float frames, in the given standard channel order, to the mixer format.
		PcmClip ConvertToMixerFormat(const std::vector<float>& samples, std::uint32_t channels, std::uint32_t rate,
			ma_standard_channel_map order, const std::filesystem::path& path)
		{
			ma_data_converter_config config = ma_data_converter_config_init(
				ma_format_f32, ma_format_f32, channels, MixerChannels, rate, MixerSampleRate);
			std::array<ma_channel, 8> map{};
			ma_channel_map_init_standard(order, map.data(), map.size(), channels);
			config.pChannelMapIn = map.data();
			// Conversion runs once at load, so use the strongest anti-aliasing filter.
			config.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
			ma_data_converter converter;
			if (const ma_result result = ma_data_converter_init(&config, nullptr, &converter); result != MA_SUCCESS)
				throw std::runtime_error(std::format("cannot convert audio ({}): {}", ma_result_description(result), PathToUtf8(path)));
			const ma_uint64 inputFrames = samples.size() / channels;
			ma_uint64 outputFrames = 0;
			ma_data_converter_get_expected_output_frame_count(&converter, inputFrames, &outputFrames);
			PcmClip clip;
			clip.Samples.resize(static_cast<std::size_t>(outputFrames) * MixerChannels);
			ma_uint64 consumed = inputFrames;
			const ma_result result = ma_data_converter_process_pcm_frames(
				&converter, samples.data(), &consumed, clip.Samples.data(), &outputFrames);
			ma_data_converter_uninit(&converter, nullptr);
			if (result != MA_SUCCESS)
				throw std::runtime_error(std::format("cannot convert audio ({}): {}", ma_result_description(result), PathToUtf8(path)));
			if (outputFrames == 0)
				throw std::runtime_error("audio clip is empty: " + PathToUtf8(path));
			clip.Frames = static_cast<std::uint32_t>(outputFrames);
			clip.Samples.resize(std::size_t{ clip.Frames } * MixerChannels);
			return clip;
		}

		// Ogg Vorbis through stb_vorbis, then the same conversion as every other format.
		PcmClip DecodeVorbis(std::span<const std::byte> bytes, const std::filesystem::path& path)
		{
			int error = 0;
			stb_vorbis* vorbis = stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
				static_cast<int>(bytes.size()), &error, nullptr);
			if (!vorbis)
				throw std::runtime_error(std::format("cannot decode Ogg Vorbis (stb_vorbis error {}): {}", error, PathToUtf8(path)));
			const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
			if (info.channels < 1 || info.channels > 8 || info.sample_rate < 8000 || info.sample_rate > 192000)
			{
				stb_vorbis_close(vorbis);
				throw std::runtime_error("Ogg Vorbis needs 1 to 8 channels at 8 to 192 kHz: " + PathToUtf8(path));
			}
			const auto channels = static_cast<std::uint32_t>(info.channels);
			const std::uint64_t maxFrames = std::uint64_t{ MaxClipSeconds } * info.sample_rate;
			std::vector<float> samples;
			constexpr std::size_t ChunkFrames = 4096;
			std::uint64_t frames = 0;
			for (;;)
			{
				samples.resize(static_cast<std::size_t>((frames + ChunkFrames) * channels));
				const int read = stb_vorbis_get_samples_float_interleaved(vorbis, info.channels,
					samples.data() + frames * channels, static_cast<int>(ChunkFrames * channels));
				if (read <= 0)
					break;
				frames += static_cast<std::uint64_t>(read);
				if (frames > maxFrames)
				{
					stb_vorbis_close(vorbis);
					throw std::runtime_error(std::format("audio is longer than {} seconds; long sounds need streaming, "
						"which is not built yet: {}", MaxClipSeconds, PathToUtf8(path)));
				}
			}
			stb_vorbis_close(vorbis);
			samples.resize(static_cast<std::size_t>(frames * channels));
			if (frames == 0)
				throw std::runtime_error("audio clip is empty: " + PathToUtf8(path));
			// Vorbis orders surround channels differently from WAV (front center second, not third).
			return ConvertToMixerFormat(samples, channels, info.sample_rate, ma_standard_channel_map_vorbis, path);
		}
	}

	bool IsAudioExtension(const std::filesystem::path& path)
	{
		return FormatOf(path) != ma_encoding_format_unknown;
	}

	PcmClip DecodeAudio(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		const auto format = FormatOf(path);
		if (format == ma_encoding_format_unknown)
			throw std::runtime_error("audio must be .wav, .flac, .mp3 or .ogg: " + PathToUtf8(path));
		if (bytes.empty() || bytes.size() > MaxAudioFileBytes)
			throw std::runtime_error("unsupported audio file size: " + PathToUtf8(path));
		if (format == ma_encoding_format_vorbis)
			return DecodeVorbis(bytes, path);

		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, MixerChannels, MixerSampleRate);
		config.encodingFormat = format;
		// Conversion runs once at load, so use the strongest anti-aliasing filter.
		config.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
		ma_decoder decoder;
		if (const ma_result result = ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder); result != MA_SUCCESS)
			throw std::runtime_error(std::format("cannot decode audio ({}): {}", ma_result_description(result), PathToUtf8(path)));

		PcmClip clip;
		constexpr ma_uint64 ChunkFrames = 4096;
		constexpr std::uint64_t MaxFrames = std::uint64_t{ MaxClipSeconds } * MixerSampleRate;
		ma_result result = MA_SUCCESS;
		std::uint64_t frames = 0;
		while (result == MA_SUCCESS)
		{
			clip.Samples.resize(static_cast<std::size_t>((frames + ChunkFrames) * MixerChannels));
			ma_uint64 read = 0;
			result = ma_decoder_read_pcm_frames(&decoder, clip.Samples.data() + frames * MixerChannels, ChunkFrames, &read);
			frames += read;
			if (frames > MaxFrames)
			{
				ma_decoder_uninit(&decoder);
				throw std::runtime_error(std::format("audio is longer than {} seconds; long sounds need streaming, "
					"which is not built yet: {}", MaxClipSeconds, PathToUtf8(path)));
			}
			if (read == 0)
				break;
		}
		ma_decoder_uninit(&decoder);
		if (result != MA_SUCCESS && result != MA_AT_END)
			throw std::runtime_error(std::format("cannot decode audio ({}): {}", ma_result_description(result), PathToUtf8(path)));
		if (frames == 0)
			throw std::runtime_error("audio clip is empty: " + PathToUtf8(path));
		clip.Frames = static_cast<std::uint32_t>(frames);
		clip.Samples.resize(static_cast<std::size_t>(frames * MixerChannels));
		clip.Samples.shrink_to_fit();
		return clip;
	}
}

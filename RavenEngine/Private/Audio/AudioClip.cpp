#include "Audio/AudioClip.hpp"
#include "Core/PathText.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		std::uint16_t ReadU16(const std::uint8_t* bytes)
		{
			return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
		}

		std::uint32_t ReadU32(const std::uint8_t* bytes)
		{
			return ReadU16(bytes) | (static_cast<std::uint32_t>(ReadU16(bytes + 2)) << 16);
		}

		bool IsChunk(const std::uint8_t* header, const char (&id)[5])
		{
			return std::equal(header, header + 4, id);
		}

	}

	PcmClip DecodeWav(std::span<const std::byte> encoded, const std::filesystem::path& path)
	{
		if (encoded.size() < 12 || encoded.size() > 16 * 1024 * 1024)
			throw std::runtime_error("unsupported WAV size: " + PathToUtf8(path));
		const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size());
		if (!IsChunk(bytes.data(), "RIFF") || !IsChunk(bytes.data() + 8, "WAVE") ||
			ReadU32(bytes.data() + 4) != bytes.size() - 8)
			throw std::runtime_error("invalid RIFF/WAVE header: " + PathToUtf8(path));

		std::span<const std::uint8_t> format;
		std::span<const std::uint8_t> samples;
		for (std::size_t offset = 12; offset < bytes.size();)
		{
			if (bytes.size() - offset < 8)
				throw std::runtime_error("truncated WAV chunk header: " + PathToUtf8(path));
			const auto* header = bytes.data() + offset;
			const std::size_t length = ReadU32(header + 4);
			offset += 8;
			if (length > bytes.size() - offset || (length % 2 && length == bytes.size() - offset))
				throw std::runtime_error("truncated WAV chunk: " + PathToUtf8(path));
			if (IsChunk(header, "fmt "))
			{
				if (format.data())
					throw std::runtime_error("duplicate WAV format chunk: " + PathToUtf8(path));
				format = { bytes.data() + offset, length };
			}
			else if (IsChunk(header, "data"))
			{
				if (samples.data())
					throw std::runtime_error("duplicate WAV data chunk: " + PathToUtf8(path));
				samples = { bytes.data() + offset, length };
			}
			offset += length + length % 2;
		}

		if (format.size() < 16 || ReadU16(format.data()) != 1 || ReadU16(format.data() + 14) != 16)
			throw std::runtime_error("WAV must use 16-bit PCM: " + PathToUtf8(path));
		const auto channels = ReadU16(format.data() + 2);
		const auto rate = ReadU32(format.data() + 4);
		if ((channels != 1 && channels != 2) || rate < 8000 || rate > 96000)
			throw std::runtime_error("WAV requires mono or stereo at 8-96 kHz: " + PathToUtf8(path));
		const auto frameBytes = channels * 2u;
		if (ReadU32(format.data() + 8) != rate * frameBytes ||
			ReadU16(format.data() + 12) != frameBytes || samples.empty() || samples.size() % frameBytes)
			throw std::runtime_error("invalid WAV sample data or block alignment: " + PathToUtf8(path));
		return { channels, rate, { samples.begin(), samples.end() } };
	}
}

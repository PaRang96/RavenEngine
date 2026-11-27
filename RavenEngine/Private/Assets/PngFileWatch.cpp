#include "Assets/PngFileWatch.hpp"

#include "Assets/ImageSource.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>

namespace Raven
{
	namespace
	{
		std::uint32_t ReadBigEndian(std::span<const std::byte> bytes)
		{
			std::uint32_t value = 0;
			for (const auto byte : bytes.first<4>())
				value = (value << 8) | std::to_integer<std::uint32_t>(byte);
			return value;
		}

		constexpr std::array<std::uint32_t, 256> BuildCrcTable()
		{
			std::array<std::uint32_t, 256> table{};
			for (std::uint32_t index = 0; index < table.size(); ++index)
			{
				std::uint32_t crc = index;
				for (int bit = 0; bit < 8; ++bit)
					crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
				table[index] = crc;
			}
			return table;
		}

		std::uint32_t ComputeCrc(std::span<const std::byte> bytes)
		{
			constexpr auto Table = BuildCrcTable();
			std::uint32_t crc = 0xffffffffu;
			for (const auto byte : bytes)
			{
				const auto index = (crc ^ std::to_integer<std::uint32_t>(byte)) & 0xffu;
				crc = (crc >> 8) ^ Table[index];
			}
			return crc ^ 0xffffffffu;
		}

		void ValidatePng(std::span<const std::byte> bytes)
		{
			constexpr std::array Signature{ std::byte{0x89}, std::byte{'P'}, std::byte{'N'},
				std::byte{'G'}, std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a} };
			if (bytes.size() < Signature.size() ||
				!std::equal(Signature.begin(), Signature.end(), bytes.begin()))
				throw std::runtime_error("expected a PNG signature");

			// The image decoder handles pixel data. Verify the complete saved stream first.
			bytes = bytes.subspan(Signature.size());
			while (bytes.size() >= 12)
			{
				const auto length = ReadBigEndian(bytes);
				if (length > bytes.size() - 12)
					break;
				const auto chunk = bytes.subspan(4, static_cast<std::size_t>(length) + 4);
				if (ComputeCrc(chunk) != ReadBigEndian(bytes.subspan(chunk.size() + 4)))
					throw std::runtime_error("PNG chunk checksum mismatch");
				bytes = bytes.subspan(chunk.size() + 8);
				if (ReadBigEndian(chunk) == 0x49454e44u) // IEND
				{
					if (length == 0 && bytes.empty())
						return;
					throw std::runtime_error("invalid PNG end chunk");
				}
			}
			throw std::runtime_error("incomplete PNG stream");
		}

		ImageData DecodePng(std::span<const std::byte> bytes, const std::filesystem::path& path)
		{
			ValidatePng(bytes);
			return Images::Decode(bytes, path);
		}
	}

	ImageData DecodePngSource(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		return DecodePng(bytes, path);
	}
}

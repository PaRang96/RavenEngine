#include "Raven/Assets/AssetId.hpp"
#include "Platform/PlatformRandom.hpp"

#include <cstddef>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		int HexValue(char value)
		{
			if (value >= '0' && value <= '9')
				return value - '0';
			if (value >= 'a' && value <= 'f')
				return value - 'a' + 10;
			if (value >= 'A' && value <= 'F')
				return value - 'A' + 10;
			return -1;
		}
	}

	AssetId AssetId::Generate()
	{
		AssetId id;
		Platform::FillRandomBytes(id.m_Bytes);
		id.m_Bytes[6] = static_cast<std::uint8_t>((id.m_Bytes[6] & 0x0f) | 0x40);
		id.m_Bytes[8] = static_cast<std::uint8_t>((id.m_Bytes[8] & 0x3f) | 0x80);
		return id;
	}

	AssetId AssetId::Parse(std::string_view text)
	{
		if (text.size() != 36)
			throw std::invalid_argument("Asset ID must be a 36-character UUIDv4");

		AssetId id;
		std::size_t cursor = 0;
		for (std::size_t byte = 0; byte < id.m_Bytes.size(); ++byte)
		{
			if (byte == 4 || byte == 6 || byte == 8 || byte == 10)
			{
				if (text[cursor++] != '-')
					throw std::invalid_argument("Asset ID has an invalid UUID separator");
			}
			const int high = HexValue(text[cursor++]);
			const int low = HexValue(text[cursor++]);
			if (high < 0 || low < 0)
				throw std::invalid_argument("Asset ID contains a non-hexadecimal character");
			id.m_Bytes[byte] = static_cast<std::uint8_t>((high << 4) | low);
		}
		if ((id.m_Bytes[6] & 0xf0) != 0x40 || (id.m_Bytes[8] & 0xc0) != 0x80)
			throw std::invalid_argument("Asset ID must use the UUIDv4 version and RFC variant");
		return id;
	}

	std::string AssetId::ToString() const
	{
		constexpr char digits[] = "0123456789abcdef";
		std::string text(36, '-');
		std::size_t cursor = 0;
		for (std::size_t byte = 0; byte < m_Bytes.size(); ++byte)
		{
			if (byte == 4 || byte == 6 || byte == 8 || byte == 10)
				++cursor;
			text[cursor++] = digits[m_Bytes[byte] >> 4];
			text[cursor++] = digits[m_Bytes[byte] & 0x0f];
		}
		return text;
	}
}

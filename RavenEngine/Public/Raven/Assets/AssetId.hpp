#pragma once

#include "Raven/Core/Api.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace Raven
{
	class AssetId
	{
	public:
		AssetId() = default;

		RAVEN_API static AssetId Generate();
		RAVEN_API static AssetId Parse(std::string_view text);
		RAVEN_API std::string ToString() const;

		constexpr bool IsValid() const
		{
			for (const auto byte : m_Bytes)
				if (byte != 0)
					return true;
			return false;
		}
		auto operator<=>(const AssetId&) const = default;

	private:
		// Canonical UUID byte order, independent of native integer endianness.
		std::array<std::uint8_t, 16> m_Bytes{};
	};
}

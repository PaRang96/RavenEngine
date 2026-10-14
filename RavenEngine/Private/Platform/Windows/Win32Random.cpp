#include "Platform/PlatformRandom.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace Raven::Platform
{
	void FillRandomBytes(std::span<std::uint8_t> bytes)
	{
		if (bytes.empty())
			return;
		if (bytes.size() > (std::numeric_limits<ULONG>::max)())
			throw std::length_error("Random byte request exceeds the Windows limit");

		const NTSTATUS status = BCryptGenRandom(nullptr, bytes.data(),
			static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
		if (status < 0)
			throw std::runtime_error("BCryptGenRandom failed with status " +
				std::to_string(static_cast<long>(status)));
	}
}

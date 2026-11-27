#pragma once

#include <cstdint>
#include <span>

// Platform area: random bytes (docs/PlatformLayerDesign.md). Implemented per OS under Platform/<OS>/.
namespace Raven::Platform
{
	// Fill the entire buffer with OS random bytes or throw; UUID rules belong to common code.
	void FillRandomBytes(std::span<std::uint8_t> bytes);
}

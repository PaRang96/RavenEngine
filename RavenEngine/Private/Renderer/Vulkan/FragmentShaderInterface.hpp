#pragma once

#include <cstdint>
#include <span>

namespace Raven
{
	// Checks the fixed textured fragment interface after SPIR-V validation.
	enum class FragmentShaderKind { Textured, Lit };
	void CheckFragmentShaderInterface(std::span<const std::uint32_t> words, FragmentShaderKind kind);
}

#pragma once

#include <cstdint>

// One version per importer, part of every reload record's key (docs/HotReloadDesign.md, Q4 A+).
// Bump an importer's version whenever its output can change for the same input bytes, so a
// future derived-data cache never reuses an output the current importer would not produce.
// Shader outputs also depend on the toolchain (DXC version and flags); a cache must key on it too.
namespace Raven::ImporterVersion
{
	inline constexpr std::uint32_t Png = 1;
	inline constexpr std::uint32_t Fbx = 1;
	// Audio clips and sound events (docs/GameServicesDesign.md, C8).
	inline constexpr std::uint32_t Audio = 1;
	inline constexpr std::uint32_t Scene = 1;
	// Material files (docs/RenderingContentDesign.md, FD-18).
	inline constexpr std::uint32_t Material = 1;
	inline constexpr std::uint32_t Project = 1;
	inline constexpr std::uint32_t Hlsl = 1;
	// Change notices only: the game module builds and verifies its own output.
	inline constexpr std::uint32_t GameSources = 1;
}

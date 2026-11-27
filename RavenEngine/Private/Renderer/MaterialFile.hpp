#pragma once

#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Math/Transform.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>

namespace Raven
{
	// A material asset (docs/RenderingContentDesign.md, FD-18): what designers set about a surface,
	// shared by every object that references it and reloaded live. Text, one setting per line:
	//
	//   raven-material 1
	//   id 2f6b0c1e-...                 the material's own asset ID
	//   base-color 0.9 0.55 0.18 1      a linear multiplier: red, green, blue, alpha, each 0 to 64
	//   texture base-color 7a590cee-... a texture asset in a named slot
	//   lit                             shaded by the scene's light; without it the surface is unlit
	//
	// B4 adds `shader` and named values. Unknown, repeated or non-finite settings are errors, so a
	// typo never silently draws a default (docs/DataModelDesign.md, version policy).
	struct MaterialDefinition
	{
		static constexpr std::size_t TextureSlots = 4;

		AssetId Id;
		Vec4 BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Slot 0 is the base color; B4's material shaders name the others.
		std::array<AssetId, TextureSlots> Textures{};
		bool Lit = false;
	};

	// Exported for the content tools, which stage a material's textures with it.
	RAVEN_API MaterialDefinition ParseMaterial(std::span<const std::byte> bytes, const std::filesystem::path& path);
}

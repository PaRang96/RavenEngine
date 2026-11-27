#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstdint>

namespace Raven::ECS
{
	// Stored in scene files: values are pinned and only appended (docs/DataModelDesign.md, FD-10).
	enum class SpriteBlendMode : std::uint8_t
	{
		Opaque = 0,
		Alpha = 1,
		// Depth-ordered like Opaque, discarding texels below alpha 0.5 (docs/RenderingContentDesign.md, B0 contract 4).
		Cutout = 2,
	};

	struct SpriteComponent : Component
	{
		// Persistent identity only; the asset cache resolves the renderer-owned texture.
		AssetRef<TextureAsset> Texture;
		Vec4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Alpha uses straight-alpha blending; opaque sprites use depth testing.
		SpriteBlendMode BlendMode = SpriteBlendMode::Alpha;
		// Alpha sprites sort by layer/order, then camera depth. Higher values draw later.
		std::int32_t SortLayer = 0;
		std::int32_t SortOrder = 0;
		// The part of the texture shown, as (u, v, width, height) in 0-1: a frame of a sprite sheet.
		Vec4 UvRect{ 0.0f, 0.0f, 1.0f, 1.0f };
		// (width, height, pivot x, pivot y): the quad's size in local units, multiplied with the
		// transform's scale, and the point of the quad at the entity's position, from its bottom left.
		Vec4 SizePivot{ 1.0f, 1.0f, 0.5f, 0.5f };

		RAVEN_COMPONENT(Raven::ECS::SpriteComponent, Texture, Tint, BlendMode, SortLayer, SortOrder, UvRect, SizePivot);
	};
}

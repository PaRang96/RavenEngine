#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstdint>

namespace Raven::ECS
{
	enum class SpriteBlendMode : std::uint8_t
	{
		Opaque,
		Alpha
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

		RAVEN_COMPONENT(Raven::ECS::SpriteComponent, Texture, Tint, BlendMode, SortLayer, SortOrder);
	};
}

#pragma once

#include "Raven/Assets/AssetId.hpp"

namespace Raven
{
	struct TextureAsset;
	struct ModelAsset;
	struct MeshAsset;
	struct MaterialAsset;
	struct SkeletonAsset;
	struct AnimationAsset;
	struct ShaderAsset;
	struct SceneAsset;

	// Persistent typed identity; does not load or own a runtime resource.
	template<typename AssetType>
	struct AssetRef
	{
		AssetId Id;

		constexpr bool IsValid() const
		{
			return Id.IsValid();
		}
		auto operator<=>(const AssetRef&) const = default;
	};
}

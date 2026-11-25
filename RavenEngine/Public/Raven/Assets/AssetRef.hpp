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
	struct AudioAsset;
	struct SoundAsset;

	// Stable asset type names for component field tables (docs/DataModelDesign.md, FD-08).
	// Renaming one changes the layout hash of every component that references the type.
	template<typename T> struct AssetTypeTraits;
	template<> struct AssetTypeTraits<TextureAsset> { static constexpr const char* Name = "Texture"; };
	template<> struct AssetTypeTraits<ModelAsset> { static constexpr const char* Name = "Model"; };
	template<> struct AssetTypeTraits<MeshAsset> { static constexpr const char* Name = "Mesh"; };
	template<> struct AssetTypeTraits<MaterialAsset> { static constexpr const char* Name = "Material"; };
	template<> struct AssetTypeTraits<SkeletonAsset> { static constexpr const char* Name = "Skeleton"; };
	template<> struct AssetTypeTraits<AnimationAsset> { static constexpr const char* Name = "Animation"; };
	template<> struct AssetTypeTraits<ShaderAsset> { static constexpr const char* Name = "Shader"; };
	template<> struct AssetTypeTraits<SceneAsset> { static constexpr const char* Name = "Scene"; };
	template<> struct AssetTypeTraits<AudioAsset> { static constexpr const char* Name = "Audio"; };
	template<> struct AssetTypeTraits<SoundAsset> { static constexpr const char* Name = "Sound"; };

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

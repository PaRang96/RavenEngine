#pragma once

#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Assets/TextureAssetDesc.hpp"
#include "Raven/Core/Api.hpp"

#include <memory>

namespace Raven
{
	// Registration and lookup use the application/rendering thread; no concurrent mutation.
	class AssetCatalog
	{
	public:
		RAVEN_API AssetCatalog();
		RAVEN_API ~AssetCatalog();

		// Add immutable metadata; duplicate IDs are rejected, including identical records.
		// Texture registration uses its dedicated overload to include color space.
		RAVEN_API void Register(const AssetRecord& record);
		RAVEN_API void Register(const TextureAssetDesc& description);
		RAVEN_API const AssetRecord& Get(const AssetId& id) const;
		RAVEN_API const TextureAssetDesc& GetTexture(AssetRef<TextureAsset> asset) const;

		AssetCatalog(const AssetCatalog&) = delete;
		AssetCatalog& operator=(const AssetCatalog&) = delete;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

#pragma once

#include "Raven/Assets/AssetId.hpp"
#include "Raven/Renderer/TextureDesc.hpp"

#include <filesystem>

namespace Raven
{
	struct TextureAssetDesc
	{
		AssetId Id;
		std::filesystem::path SourcePath;
		TextureColorSpace ColorSpace = TextureColorSpace::SRGB;
	};
}

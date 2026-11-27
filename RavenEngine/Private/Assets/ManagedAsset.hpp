#pragma once

#include "Assets/FbxMesh.hpp"
#include "Assets/SettledFileWatch.hpp"
#include "Audio/AudioClip.hpp"
#include "Audio/SoundEvent.hpp"
#include "Renderer/MaterialFile.hpp"
#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Assets/Image.hpp"
#include "Raven/Renderer/MeshHandle.hpp"
#include "Raven/Renderer/TextureHandle.hpp"
#include "Raven/Renderer/TextureDesc.hpp"

#include <memory>
#include <variant>

namespace Raven
{
	class VulkanContext;

	struct DecodedAsset
	{
		std::vector<std::byte> Bytes;
		std::variant<FbxMesh, ImageData, PcmClip, SoundEvent, MaterialDefinition, MaterialShader> Data;
	};

	struct ManagedAsset
	{
		explicit ManagedAsset(VulkanContext& renderer) : Renderer(renderer) {}
		~ManagedAsset();
		bool IsCurrent() const;
		void Commit(ManagedAsset& candidate) noexcept;

		VulkanContext& Renderer;
		AssetRecord Record;
		TextureColorSpace ColorSpace = TextureColorSpace::SRGB;
		std::filesystem::path Source;
		std::vector<std::byte> Bytes;
		MeshHandle Mesh;
		TextureHandle Texture;
		std::shared_ptr<const PcmClip> Clip;
		std::shared_ptr<const SoundEvent> Sound;
		std::shared_ptr<const MaterialDefinition> Material;
		std::shared_ptr<const MaterialShader> Shader;
		// Bumped by every committed reload and by a background load's arrival, so holders of a lease can
		// tell new contents.
		std::uint64_t Revision = 0;
		AssetLoadState State = AssetLoadState::Ready;
		std::unique_ptr<SettledFileWatch<DecodedAsset>> Watch;
	};
}

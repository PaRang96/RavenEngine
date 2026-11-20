#include "Raven/Assets/AssetResources.hpp"

#include "Assets/FbxMesh.hpp"
#include "Assets/PngFileWatch.hpp"
#include "Assets/SettledFileWatch.hpp"
#include "Core/PathText.hpp"
#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Assets/Image.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Raven
{
	struct AssetResources::Impl
	{
		explicit Impl(const AssetCatalog& catalog, VulkanContext& renderer)
			: Catalog(catalog), Renderer(renderer), ContentRoot(Paths::GetContentPath())
		{
		}

		const VulkanTexture& LoadTexture(AssetRef<TextureAsset> asset);
		MeshHandle LoadMesh(AssetRef<MeshAsset> asset, const std::filesystem::path* watchSource = nullptr);

		const AssetCatalog& Catalog;
		VulkanContext& Renderer;
		const std::filesystem::path ContentRoot;
		std::map<AssetId, const VulkanTexture*> Textures;
		struct MeshResource
		{
			MeshHandle Handle;
			std::optional<SettledFileWatch<FbxMesh>> Watch;
		};
		std::map<AssetId, MeshResource> Meshes;

		struct WatchedTexture
		{
			WatchedTexture(const TextureAssetDesc& description, const VulkanTexture& texture,
				const std::filesystem::path& source)
				: ColorSpace(description.ColorSpace), Texture(texture), Watch(source)
			{
			}

			TextureColorSpace ColorSpace;
			const VulkanTexture& Texture;
			PngFileWatch Watch;
		};
		std::map<AssetId, std::unique_ptr<WatchedTexture>> Watched;
	};

	const VulkanTexture& AssetResources::Impl::LoadTexture(AssetRef<TextureAsset> asset)
	{
		const auto& description = Catalog.GetTexture(asset);
		if (const auto found = Textures.find(asset.Id); found != Textures.end())
			return *found->second;

		// Allocate cache metadata before CreateTexture publishes a context-owned GPU resource.
		const auto entry = Textures.try_emplace(asset.Id, nullptr).first;
		try
		{
			const ImageData image = Images::Load(ContentRoot / description.SourcePath);
			const auto& texture = Renderer.CreateTexture(
				{ image.Width, image.Height, image.Pixels, description.ColorSpace });
			entry->second = &texture;
			return texture;
		}
		catch (...)
		{
			Textures.erase(entry);
			throw;
		}
	}

	AssetResources::AssetResources(const AssetCatalog& catalog, VulkanContext& renderer)
		: m_Impl(std::make_unique<Impl>(catalog, renderer))
	{
	}

	AssetResources::~AssetResources() = default;

	const VulkanTexture& AssetResources::LoadTexture(AssetRef<TextureAsset> asset)
	{
		return m_Impl->LoadTexture(asset);
	}

	MeshHandle AssetResources::LoadMesh(AssetRef<MeshAsset> asset)
	{
		return m_Impl->LoadMesh(asset);
	}

	MeshHandle AssetResources::Impl::LoadMesh(AssetRef<MeshAsset> asset, const std::filesystem::path* watchSource)
	{
		const auto& record = Catalog.Get(asset.Id);
		if (record.Kind != AssetKind::Mesh)
			throw std::invalid_argument("Asset does not refer to a mesh: " + asset.Id.ToString());
		if (const auto found = Meshes.find(asset.Id); found != Meshes.end())
		{
			auto& resource = found->second;
			if (watchSource)
			{
				if (resource.Watch)
					throw std::logic_error("This mesh asset is already watched");
				resource.Watch.emplace(*watchSource, "FBX", ReadFbxBytes, DecodeFbxMesh);
			}
			return resource.Handle;
		}

		const auto path = ContentRoot / record.SourcePath;
		auto bytes = ReadFbxBytes(path);
		const auto mesh = DecodeFbxMesh(bytes, path);
		const auto entry = Meshes.try_emplace(asset.Id).first;
		try
		{
			auto& resource = entry->second;
			if (watchSource)
				resource.Watch.emplace(*watchSource, "FBX", ReadFbxBytes, DecodeFbxMesh, std::move(bytes));
			resource.Handle = Renderer.CreateMeshHandle(mesh.Vertices, mesh.Indices);
			return resource.Handle;
		}
		catch (...)
		{
			Meshes.erase(entry);
			throw;
		}
	}

	void AssetResources::WatchFbx(AssetRef<MeshAsset> asset, const std::filesystem::path& source)
	{
		m_Impl->LoadMesh(asset, &source);
	}

	bool AssetResources::ApplyMeshReloads()
	{
		bool replaced = false;
		for (auto& [id, resource] : m_Impl->Meshes)
		{
			if (!resource.Watch)
				continue;
			const auto mesh = resource.Watch->Take();
			if (!mesh)
				continue;
			try
			{
				m_Impl->Renderer.ReplaceMesh(resource.Handle, mesh->Vertices, mesh->Indices);
				replaced = true;
				Log(LogLevel::Info, "Replaced FBX " + PathToUtf8(resource.Watch->GetPath()));
			}
			catch (const std::exception& error)
			{
				Log(LogLevel::Warning, "Cannot upload reloaded FBX " +
					PathToUtf8(resource.Watch->GetPath()) + ": " + error.what());
			}
		}
		return replaced;
	}

	void AssetResources::WatchPng(AssetRef<TextureAsset> asset, const std::filesystem::path& source)
	{
		if (m_Impl->Watched.contains(asset.Id))
			throw std::logic_error("This texture asset is already watched");
		const auto& description = m_Impl->Catalog.GetTexture(asset);
		const auto& texture = LoadTexture(asset);
		m_Impl->Watched.emplace(asset.Id, std::make_unique<Impl::WatchedTexture>(description, texture, source));
	}

	bool AssetResources::IsWatching(AssetRef<TextureAsset> asset) const noexcept
	{
		return m_Impl->Watched.contains(asset.Id);
	}

	bool AssetResources::ApplyTextureReload()
	{
		bool replaced = false;
		for (auto& [id, watch] : m_Impl->Watched)
		{
			const auto image = watch->Watch.TakeImage();
			if (!image)
				continue;
			try
			{
				m_Impl->Renderer.ReplaceTexture(watch->Texture,
					{ image->Width, image->Height, image->Pixels, watch->ColorSpace });
				replaced = true;
				Log(LogLevel::Info, "Replaced PNG " + PathToUtf8(watch->Watch.GetPath()));
			}
			catch (const std::exception& error)
			{
				Log(LogLevel::Warning, "Cannot upload reloaded PNG " + PathToUtf8(watch->Watch.GetPath()) +
					": " + error.what());
			}
		}
		return replaced;
	}
}

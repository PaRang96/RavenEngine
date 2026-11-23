#include "Raven/Assets/AssetStore.hpp"

#include "Assets/AssetPath.hpp"
#include "Assets/ManagedAsset.hpp"
#include "Assets/PngFileWatch.hpp"
#include "Audio/AudioClip.hpp"
#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"
#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <stdexcept>
#include <utility>

namespace Raven
{
	namespace
	{
		std::vector<std::byte> ReadAsset(const std::filesystem::path& path)
		{
			return Files::ReadBinary(path, 64 * 1024 * 1024);
		}

		DecodedAsset DecodeAsset(std::span<const std::byte> bytes, const std::filesystem::path& path)
		{
			DecodedAsset result;
			result.Bytes.assign(bytes.begin(), bytes.end());
			auto extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});
			if (extension == ".fbx")
				result.Data = DecodeFbxMesh(bytes, path);
			else if (extension == ".png")
				result.Data = DecodePngSource(bytes, path);
			else if (extension == ".wav")
				result.Data = DecodeWav(bytes, path);
			else
				throw std::invalid_argument("Unsupported managed asset extension: " + extension);
			return result;
		}
	}

	ManagedAsset::~ManagedAsset()
	{
		Watch.reset();
		if (!Mesh.IsNull())
			Renderer.ReleaseMesh(Mesh);
		if (!Texture.IsNull())
			Renderer.ReleaseTexture(Texture);
	}

	bool ManagedAsset::IsCurrent() const
	{
		return ReadAsset(Source) == Bytes;
	}

	void ManagedAsset::Commit(ManagedAsset& candidate) noexcept
	{
		if (!Mesh.IsNull())
			Renderer.SwapMeshContents(Mesh, candidate.Mesh);
		if (!Texture.IsNull())
			Renderer.SwapTextureContents(Texture, candidate.Texture);
		Clip.swap(candidate.Clip);
		std::swap(Record, candidate.Record);
		std::swap(ColorSpace, candidate.ColorSpace);
		Source.swap(candidate.Source);
		Bytes.swap(candidate.Bytes);
		Watch.swap(candidate.Watch);
	}

	struct AssetStore::Impl
	{
		Impl(VulkanContext& renderer, std::filesystem::path root, bool meshes, bool textures)
			: Renderer(renderer), Root(std::filesystem::absolute(std::move(root))),
			  WatchMeshes(meshes), WatchTextures(textures) {}

		std::shared_ptr<ManagedAsset> Load(const AssetCatalog& catalog, AssetId id,
			std::optional<DecodedAsset> decoded = std::nullopt)
		{
			auto result = std::make_shared<ManagedAsset>(Renderer);
			result->Record = catalog.Get(id);
			const auto& record = result->Record;
			result->Source = ResolveAssetPath(Root, record.SourcePath);
			if (!decoded)
				decoded = DecodeAsset(ReadAsset(result->Source), result->Source);
			if (record.Kind == AssetKind::Mesh)
			{
				const auto& mesh = std::get<FbxMesh>(decoded->Data);
				result->Mesh = Renderer.CreateManagedMesh(mesh.Vertices, mesh.Indices);
			}
			else if (record.Kind == AssetKind::Texture)
			{
				result->ColorSpace = catalog.GetTexture({ id }).ColorSpace;
				const auto& image = std::get<ImageData>(decoded->Data);
				result->Texture = Renderer.CreateManagedTexture({ image.Width, image.Height, image.Pixels, result->ColorSpace });
			}
			else if (record.Kind == AssetKind::Audio)
				result->Clip = std::make_shared<const PcmClip>(std::move(std::get<PcmClip>(decoded->Data)));
			else
				throw std::invalid_argument("Asset kind cannot be acquired by a scene");
			result->Bytes = std::move(decoded->Bytes);
			if ((record.Kind == AssetKind::Mesh && WatchMeshes) || (record.Kind == AssetKind::Texture && WatchTextures))
			{
				const bool mesh = record.Kind == AssetKind::Mesh;
				result->Watch = std::make_unique<SettledFileWatch<DecodedAsset>>(result->Source, mesh ? "FBX" : "PNG",
					mesh ? ImporterVersion::Fbx : ImporterVersion::Png, ReadAsset, DecodeAsset, result->Bytes);
				Log(LogLevel::Info, "Watching asset " + id.ToString() + " " + PathToUtf8(result->Source));
			}
			return result;
		}

		VulkanContext& Renderer;
		const std::filesystem::path Root;
		const bool WatchMeshes;
		const bool WatchTextures;
		std::map<AssetId, std::weak_ptr<ManagedAsset>> Entries;
	};

	struct AssetCatalogUpdate::Impl
	{
		struct Change
		{
			std::shared_ptr<ManagedAsset> Current;
			std::shared_ptr<ManagedAsset> Candidate;
		};
		std::vector<Change> Changes;
	};

	AssetCatalogUpdate::AssetCatalogUpdate() : m_Impl(std::make_unique<Impl>()) {}
	AssetCatalogUpdate::~AssetCatalogUpdate() = default;
	AssetCatalogUpdate::AssetCatalogUpdate(AssetCatalogUpdate&&) noexcept = default;
	AssetCatalogUpdate& AssetCatalogUpdate::operator=(AssetCatalogUpdate&&) noexcept = default;

	bool AssetCatalogUpdate::IsCurrent() const
	{
		for (const auto& change : m_Impl->Changes)
			if (!change.Candidate->IsCurrent())
				return false;
		return true;
	}

	void AssetCatalogUpdate::Commit() noexcept
	{
		for (auto& change : m_Impl->Changes)
			change.Current->Commit(*change.Candidate);
		m_Impl->Changes.clear();
	}

	AssetStore::AssetStore(VulkanContext& renderer, std::filesystem::path root, bool meshes, bool textures)
		: m_Impl(std::make_unique<Impl>(renderer, std::move(root), meshes, textures)) {}
	AssetStore::~AssetStore() = default;

	AssetLease AssetStore::Acquire(const AssetCatalog& catalog, AssetId id, AssetKind kind)
	{
		if (catalog.Get(id).Kind != kind)
			throw std::invalid_argument("Incorrect scene asset type: " + id.ToString());
		if (!std::filesystem::is_regular_file(ResolveAssetPath(m_Impl->Root, catalog.Get(id).SourcePath)))
			throw std::runtime_error("Missing referenced scene asset: " + id.ToString());
		Collect();
		auto& entry = m_Impl->Entries[id];
		auto asset = entry.lock();
		if (!asset)
		{
			asset = m_Impl->Load(catalog, id);
			entry = asset;
		}
		if (asset->Record.Kind != kind)
			throw std::invalid_argument("Cannot change a live asset's type: " + id.ToString());
		return AssetLease(std::move(asset));
	}

	AssetCatalogUpdate AssetStore::PrepareCatalog(const AssetCatalog& catalog)
	{
		Collect();
		AssetCatalogUpdate update;
		for (const auto& [id, weak] : m_Impl->Entries)
			if (auto current = weak.lock())
			{
				const auto& record = catalog.Get(id);
				if (record.Kind != current->Record.Kind)
					throw std::invalid_argument("Cannot change a live asset's type: " + id.ToString());
				const auto color = record.Kind == AssetKind::Texture ? catalog.GetTexture({ id }).ColorSpace : current->ColorSpace;
				if (record.SourcePath != current->Record.SourcePath || color != current->ColorSpace)
					update.m_Impl->Changes.push_back({ current, m_Impl->Load(catalog, id) });
			}
		return update;
	}

	AssetReloadResult AssetStore::ApplyReloads()
	{
		Collect();
		AssetReloadResult result;
		for (const auto& [id, weak] : m_Impl->Entries)
		{
			auto current = weak.lock();
			if (!current || !current->Watch)
				continue;
			auto decoded = current->Watch->Take();
			if (!decoded)
				continue;
			try
			{
				const auto publishStart = std::chrono::steady_clock::now();
				AssetCatalog catalog;
				if (current->Record.Kind == AssetKind::Texture)
					catalog.Register(TextureAssetDesc{ id, current->Record.SourcePath, current->ColorSpace });
				else
					catalog.Register(current->Record);
				auto candidate = m_Impl->Load(catalog, id, std::move(decoded));
				if (!candidate->IsCurrent())
					continue;
				// Commit swaps in the candidate's fresh watch, so the summary comes from this one.
				const std::chrono::duration<double, std::milli> publish = std::chrono::steady_clock::now() - publishStart;
				const auto summary = current->Watch->Published(publish);
				current->Commit(*candidate);
				result.Mesh |= current->Record.Kind == AssetKind::Mesh;
				result.Texture |= current->Record.Kind == AssetKind::Texture;
				// The H7 summary: trigger, import and publish times.
				Log(LogLevel::Info, std::string(current->Record.Kind == AssetKind::Mesh ? "Replaced FBX " : "Replaced PNG ") +
					PathToUtf8(current->Source) + "; " + summary);
			}
			catch (const std::exception& error)
			{
				Log(LogLevel::Warning, "Cannot publish asset " + id.ToString() + ": " + error.what());
			}
		}
		return result;
	}

	void AssetStore::Collect()
	{
		std::erase_if(m_Impl->Entries, [](const auto& entry) { return entry.second.expired(); });
		m_Impl->Renderer.CollectResources();
	}

	AssetStoreStats AssetStore::GetStats() const noexcept
	{
		AssetStoreStats result;
		result.CachedEntries = m_Impl->Entries.size();
		for (const auto& [id, weak] : m_Impl->Entries)
			if (const auto asset = weak.lock())
			{
				++result.ResidentAssets;
				result.Watchers += asset->Watch != nullptr;
			}
		return result;
	}

	AssetId AssetLease::GetId() const noexcept { return m_Asset ? m_Asset->Record.Id : AssetId{}; }
	AssetKind AssetLease::GetKind() const noexcept { return m_Asset ? m_Asset->Record.Kind : AssetKind::Unknown; }

	MeshHandle AssetLease::GetMesh() const
	{
		if (GetKind() != AssetKind::Mesh)
			throw std::invalid_argument("Lease does not own a mesh");
		return m_Asset->Mesh;
	}

	TextureHandle AssetLease::GetTextureHandle() const
	{
		if (GetKind() != AssetKind::Texture)
			throw std::invalid_argument("Lease does not own a texture");
		return m_Asset->Texture;
	}

	const VulkanTexture& AssetLease::GetTexture() const
	{
		const auto* texture = m_Asset ? m_Asset->Renderer.ResolveTexture(GetTextureHandle()) : nullptr;
		if (!texture)
			throw std::invalid_argument("Lease does not own a live texture");
		return *texture;
	}
}

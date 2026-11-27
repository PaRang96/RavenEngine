#include "Raven/Assets/AssetStore.hpp"

#include "Assets/AssetKinds.hpp"
#include "Assets/AssetPath.hpp"
#include "Assets/ManagedAsset.hpp"
#include "Assets/PngFileWatch.hpp"
#include "Audio/AudioClip.hpp"
#include "Core/ImporterVersions.hpp"
#include "Raven/Assets/AssetId.hpp"
#include "Renderer/Shaders/ShaderLibrary.hpp"
#include "Renderer/Vulkan/LitShaderInterfaces.hpp"
#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"
#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
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

		// A material shader (B0 contract 7): compiled from its HLSL in development, or loaded as the SPIR-V a
		// player build staged; either way checked against the lit fragment interface and reflected.
		MaterialShader DecodeShader(std::span<const std::byte> bytes, const std::filesystem::path& path)
		{
			MaterialShader shader;
			if (LowercaseExtension(path) == ".spv")
			{
				if (bytes.size() % sizeof(std::uint32_t) != 0)
					throw std::runtime_error(PathToUtf8(path) + " is not SPIR-V");
				shader.Words.resize(bytes.size() / sizeof(std::uint32_t));
				std::memcpy(shader.Words.data(), bytes.data(), bytes.size());
			}
			else
			{
				const auto scratch = std::filesystem::temp_directory_path() / ("raven-shader-" + AssetId::Generate().ToString());
				auto compiled = CompileHlsl(path, "ps_6_0", "PSMain", scratch);
				std::error_code ignored;
				std::filesystem::remove_all(scratch, ignored);
				if (!compiled.Error.empty())
					throw std::runtime_error("Cannot compile material shader " + PathToUtf8(path) + ": " + compiled.Error);
				shader.Words = std::move(compiled.Words);
			}
			CheckShaderInterface(shader.Words, LitFragmentInterface());
			shader.Parameters = ReadMaterialParameters(shader.Words);
			return shader;
		}

		DecodedAsset DecodeAsset(std::span<const std::byte> bytes, const std::filesystem::path& path)
		{
			DecodedAsset result;
			result.Bytes.assign(bytes.begin(), bytes.end());
			const auto extension = LowercaseExtension(path);
			if (extension == ".fbx")
				result.Data = DecodeFbxMesh(bytes, path);
			else if (extension == ".png")
				result.Data = DecodePngSource(bytes, path);
			else if (IsAudioExtension(path))
				result.Data = DecodeAudio(bytes, path);
			else if (extension == ".ravensound")
				result.Data = ParseSoundEvent(bytes, path);
			else if (extension == ".ravenmaterial")
				result.Data = ParseMaterial(bytes, path);
			else if (extension == ".hlsl" || extension == ".spv")
				result.Data = DecodeShader(bytes, path);
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
		Sound.swap(candidate.Sound);
		Material.swap(candidate.Material);
		Shader.swap(candidate.Shader);
		++Revision;
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
			else if (record.Kind == AssetKind::Sound)
				result->Sound = std::make_shared<const SoundEvent>(std::move(std::get<SoundEvent>(decoded->Data)));
			else if (record.Kind == AssetKind::Material)
			{
				auto material = std::get<MaterialDefinition>(std::move(decoded->Data));
				// The file's header names the asset; a copied file keeps the original's ID until the
				// content scan gives it its own.
				if (material.Id != id)
					throw std::runtime_error("Material " + PathToUtf8(result->Source) + " names " + material.Id.ToString() +
						" in its header, not " + id.ToString());
				result->Material = std::make_shared<const MaterialDefinition>(std::move(material));
			}
			else if (record.Kind == AssetKind::Shader)
				result->Shader = std::make_shared<const MaterialShader>(std::get<MaterialShader>(std::move(decoded->Data)));
			else
				throw std::invalid_argument("Asset kind cannot be acquired by a scene");
			result->Bytes = std::move(decoded->Bytes);
			// Content watching covers textures, and with them audio clips, sound events (C8) and materials.
			const bool sound = record.Kind == AssetKind::Audio || record.Kind == AssetKind::Sound;
			const bool material = record.Kind == AssetKind::Material;
			const bool shader = record.Kind == AssetKind::Shader;
			if ((record.Kind == AssetKind::Mesh && WatchMeshes) ||
				((record.Kind == AssetKind::Texture || sound || material || shader) && WatchTextures))
			{
				const bool mesh = record.Kind == AssetKind::Mesh;
				result->Watch = std::make_unique<SettledFileWatch<DecodedAsset>>(result->Source, AssetKindName(record.Kind),
					mesh ? ImporterVersion::Fbx : sound ? ImporterVersion::Audio : material ? ImporterVersion::Material
					: shader ? ImporterVersion::Hlsl : ImporterVersion::Png, ReadAsset, DecodeAsset, result->Bytes);
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

	const char* AssetStore::FindReferenceError(const AssetCatalog& catalog, AssetId id, AssetKind kind) const
	{
		const auto* record = catalog.Find(id);
		if (!record)
			return "unregistered asset ID";
		if (record->Kind != kind)
			return "incorrect asset type";
		std::error_code error;
		if (!std::filesystem::is_regular_file(ResolveAssetPath(m_Impl->Root, record->SourcePath), error))
			return "missing asset file";
		return nullptr;
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
				result.Material |= current->Record.Kind == AssetKind::Material;
				// The H7 summary: trigger, import and publish times.
				Log(LogLevel::Info, "Replaced " + std::string(AssetKindName(current->Record.Kind)) + " " +
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
	const SoundEvent* AssetLease::GetSoundEvent() const noexcept
	{
		return m_Asset ? m_Asset->Sound.get() : nullptr;
	}

	const MaterialDefinition* AssetLease::GetMaterial() const noexcept
	{
		return m_Asset ? m_Asset->Material.get() : nullptr;
	}

	const MaterialShader* AssetLease::GetMaterialShader() const noexcept
	{
		return m_Asset ? m_Asset->Shader.get() : nullptr;
	}

	std::uint64_t AssetLease::GetRevision() const noexcept
	{
		return m_Asset ? m_Asset->Revision : 0;
	}

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

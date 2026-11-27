#include "Raven/Assets/AssetStore.hpp"

#include "Assets/AssetKinds.hpp"
#include "Assets/AssetPath.hpp"
#include "Assets/ManagedAsset.hpp"
#include "Assets/PngFileWatch.hpp"
#include "Audio/AudioClip.hpp"
#include "Core/ImporterVersions.hpp"
#include "Core/JobSystem.hpp"
#include "Raven/Assets/AssetId.hpp"
#include "Renderer/Shaders/ShaderLibrary.hpp"
#include "Renderer/Vulkan/LitShaderInterfaces.hpp"
#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"
#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
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
			const auto toWords = [&](std::span<const std::byte> spirv)
			{
				if (spirv.size() % sizeof(std::uint32_t) != 0)
					throw std::runtime_error(PathToUtf8(path) + " is not SPIR-V");
				std::vector<std::uint32_t> words(spirv.size() / sizeof(std::uint32_t));
				std::memcpy(words.data(), spirv.data(), spirv.size());
				return words;
			};
			if (LowercaseExtension(path) == ".spv")
			{
				shader.Words = toWords(bytes);
				// A player build stages a vertex offset's program beside it (B0 contract 2).
				auto vertex = path;
				vertex.replace_extension(".vert.spv");
				if (std::filesystem::is_regular_file(vertex))
					shader.VertexWords = toWords(Files::ReadBinary(vertex, 4 * 1024 * 1024));
			}
			else
			{
				const auto scratch = std::filesystem::temp_directory_path() / ("raven-shader-" + AssetId::Generate().ToString());
				const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove_all(scratch, ignored); };
				auto compiled = CompileHlsl(path, "ps_6_0", "PSMain", scratch);
				CompiledHlsl vertex;
				if (compiled.Error.empty() && DeclaresVertexOffset(bytes))
					vertex = CompileOffsetVertex(path, scratch / "vertex");
				cleanup();
				if (!compiled.Error.empty())
					throw std::runtime_error("Cannot compile material shader " + PathToUtf8(path) + ": " + compiled.Error);
				if (!vertex.Error.empty())
					throw std::runtime_error("Cannot compile the vertex offset of material shader " + PathToUtf8(path) + ": " +
						vertex.Error);
				shader.Words = std::move(compiled.Words);
				shader.VertexWords = std::move(vertex.Words);
			}
			CheckShaderInterface(shader.Words, LitFragmentInterface());
			shader.Parameters = ReadMaterialParameters(shader.Words);
			if (!shader.VertexWords.empty())
			{
				CheckShaderInterface(shader.VertexWords, LitVertexInterface());
				const auto bound = ReadOffsetBound(shader.VertexWords);
				// The bound grows culling bounds, so it has to be a real, modest distance.
				if (!bound || !std::isfinite(*bound) || *bound <= 0.0f || *bound > 100.0f)
					throw std::runtime_error("Material shader " + PathToUtf8(path) +
						" needs RAVEN_OFFSET_BOUND between 0 and 100 metres with its RavenOffset");
				shader.OffsetBound = *bound;
			}
			return shader;
		}

		// A model part decodes its node's mesh; every other asset its whole file (FD-23).
		DecodedAsset DecodeAsset(std::span<const std::byte> bytes, const std::filesystem::path& path,
			const std::string& part = {})
		{
			DecodedAsset result;
			result.Bytes.assign(bytes.begin(), bytes.end());
			result.BytesHash = ReloadService::HashContent(bytes);
			const auto extension = LowercaseExtension(path);
			if (extension == ".fbx")
				result.Data = DecodeFbxMesh(bytes, path, part);
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
		const auto bytes = ReadAsset(Source);
		return bytes.size() == BytesSize && ReloadService::HashContent(bytes) == BytesHash;
	}

	void ManagedAsset::Commit(ManagedAsset& candidate) noexcept
	{
		// A loaded asset's contents are rewritten in place; one that had none (a background load arriving)
		// takes the candidate's resources, a new mesh and a new bindless element (B0 contract 5).
		if (!Mesh.IsNull() && !candidate.Mesh.IsNull())
			Renderer.SwapMeshContents(Mesh, candidate.Mesh);
		else
			std::swap(Mesh, candidate.Mesh);
		if (!Texture.IsNull() && !candidate.Texture.IsNull())
			Renderer.SwapTextureContents(Texture, candidate.Texture);
		else
			std::swap(Texture, candidate.Texture);
		State = AssetLoadState::Ready;
		Clip.swap(candidate.Clip);
		Sound.swap(candidate.Sound);
		Material.swap(candidate.Material);
		Shader.swap(candidate.Shader);
		++Revision;
		std::swap(Record, candidate.Record);
		std::swap(ColorSpace, candidate.ColorSpace);
		Source.swap(candidate.Source);
		std::swap(BytesSize, candidate.BytesSize);
		std::swap(BytesHash, candidate.BytesHash);
		Watch.swap(candidate.Watch);
	}

	namespace
	{
		// One background load (FD-22): the worker reads and decodes the file; the main thread publishes it.
		struct BackgroundLoad
		{
			std::filesystem::path Source;
			std::string Part;
			std::optional<DecodedAsset> Decoded;
			std::string Error;
			double DecodeMs = 0.0;
			std::atomic<bool> Done{ false };
		};

		// Loads still running, so the store can wait for them before it goes away.
		struct LoadTracker
		{
			std::atomic<std::uint32_t> Running{ 0 };
		};

		struct LoadJob
		{
			std::shared_ptr<BackgroundLoad> Load;
			std::shared_ptr<LoadTracker> Tracker;
		};

		void RunLoad(void* context, std::uint32_t) noexcept
		{
			const std::unique_ptr<LoadJob> job(static_cast<LoadJob*>(context));
			auto& load = *job->Load;
			const auto start = std::chrono::steady_clock::now();
			try
			{
				load.Decoded = DecodeAsset(ReadAsset(load.Source), load.Source, load.Part);
			}
			catch (const std::exception& error)
			{
				try
				{
					load.Error = error.what();
				}
				catch (...)
				{
				}
			}
			catch (...)
			{
			}
			load.DecodeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			load.Done.store(true, std::memory_order_release);
			if (job->Tracker->Running.fetch_sub(1, std::memory_order_acq_rel) == 1)
				job->Tracker->Running.notify_all();
		}
	}

	struct AssetStore::Impl
	{
		Impl(VulkanContext& renderer, std::filesystem::path root, bool meshes, bool textures)
			: Renderer(renderer), Root(std::filesystem::absolute(std::move(root))),
			  WatchMeshes(meshes), WatchTextures(textures) {}

		~Impl()
		{
			// Workers hold only the load's own state, but the decoders must finish before the content they
			// read is torn down with the session.
			for (auto running = Tracker->Running.load(); running != 0; running = Tracker->Running.load())
				Tracker->Running.wait(running);
		}

		// The catalog record of an asset, as Load reads it.
		static AssetCatalog RecordOf(const ManagedAsset& asset)
		{
			AssetCatalog catalog;
			if (asset.Record.Kind == AssetKind::Texture)
				catalog.Register(TextureAssetDesc{ asset.Record.Id, asset.Record.SourcePath, asset.ColorSpace });
			else
				catalog.Register(asset.Record);
			return catalog;
		}

		// Publishes a finished background load into its asset; a failed one is reported once.
		bool Publish(ManagedAsset& asset, BackgroundLoad& load)
		{
			if (asset.State != AssetLoadState::Loading)
				return false;
			try
			{
				if (!load.Decoded)
					throw std::runtime_error(load.Error.empty() ? "the decoder failed" : load.Error);
				const auto start = std::chrono::steady_clock::now();
				auto candidate = Load(RecordOf(asset), asset.Record.Id, std::move(load.Decoded));
				asset.Commit(*candidate);
				const std::chrono::duration<double, std::milli> publish = std::chrono::steady_clock::now() - start;
				Log(LogLevel::Info, std::format("Loaded {} {} in the background: decode {:.3f} ms, publish {:.3f} ms",
					AssetKindName(asset.Record.Kind), PathToUtf8(asset.Source), load.DecodeMs, publish.count()));
				return true;
			}
			catch (const std::exception& error)
			{
				asset.State = AssetLoadState::Failed;
				Log(LogLevel::Warning, "Asset " + asset.Record.Id.ToString() + " could not be loaded and keeps its "
					"placeholder until its scene reloads: " + error.what());
				return false;
			}
		}

		// Finishes a loading asset on the calling thread, for a scene that needs it now.
		void LoadNow(ManagedAsset& asset)
		{
			auto candidate = Load(RecordOf(asset), asset.Record.Id);
			asset.Commit(*candidate);
		}

		std::shared_ptr<ManagedAsset> Load(const AssetCatalog& catalog, AssetId id,
			std::optional<DecodedAsset> decoded = std::nullopt)
		{
			auto result = std::make_shared<ManagedAsset>(Renderer);
			result->Record = catalog.Get(id);
			const auto& record = result->Record;
			result->Source = ResolveAssetPath(Root, record.SourcePath);
			if (!decoded)
				decoded = DecodeAsset(ReadAsset(result->Source), result->Source, record.Part);
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
			result->BytesSize = decoded->Bytes.size();
			result->BytesHash = decoded->BytesHash;
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
					: shader ? ImporterVersion::Hlsl : ImporterVersion::Png, ReadAsset,
					[part = record.Part](std::span<const std::byte> bytes, const std::filesystem::path& path)
					{
						return DecodeAsset(bytes, path, part);
					}, std::move(decoded->Bytes), false, result->BytesHash);
				Log(LogLevel::Info, "Watching asset " + id.ToString() + " " + PathToUtf8(result->Source));
			}
			return result;
		}

		VulkanContext& Renderer;
		const std::filesystem::path Root;
		const bool WatchMeshes;
		const bool WatchTextures;
		std::map<AssetId, std::weak_ptr<ManagedAsset>> Entries;
		JobSystem* Workers = nullptr;
		struct Pending
		{
			std::weak_ptr<ManagedAsset> Asset;
			std::shared_ptr<BackgroundLoad> Load;
		};
		std::vector<Pending> Loads;
		std::shared_ptr<LoadTracker> Tracker = std::make_shared<LoadTracker>();
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
		// A scene preloads what it references, so an asset still loading in the background is finished
		// here; its worker's result is then dropped.
		if (asset->State != AssetLoadState::Ready)
			m_Impl->LoadNow(*asset);
		return AssetLease(std::move(asset));
	}

	AssetLease AssetStore::Request(const AssetCatalog& catalog, AssetId id, AssetKind kind)
	{
		if (!m_Impl->Workers)
			return Acquire(catalog, id, kind);
		if (catalog.Get(id).Kind != kind)
			throw std::invalid_argument("Incorrect scene asset type: " + id.ToString());
		const auto source = ResolveAssetPath(m_Impl->Root, catalog.Get(id).SourcePath);
		if (!std::filesystem::is_regular_file(source))
			throw std::runtime_error("Missing referenced scene asset: " + id.ToString());
		Collect();
		auto& entry = m_Impl->Entries[id];
		if (auto asset = entry.lock())
		{
			if (asset->Record.Kind != kind)
				throw std::invalid_argument("Cannot change a live asset's type: " + id.ToString());
			return AssetLease(std::move(asset));
		}
		// No contents until the worker's decode is published (FD-22).
		auto asset = std::make_shared<ManagedAsset>(m_Impl->Renderer);
		asset->Record = catalog.Get(id);
		asset->Source = source;
		if (kind == AssetKind::Texture)
			asset->ColorSpace = catalog.GetTexture({ id }).ColorSpace;
		asset->State = AssetLoadState::Loading;
		auto load = std::make_shared<BackgroundLoad>();
		load->Source = source;
		load->Part = asset->Record.Part;
		m_Impl->Loads.push_back({ asset, load });
		auto job = std::make_unique<LoadJob>(LoadJob{ load, m_Impl->Tracker });
		m_Impl->Tracker->Running.fetch_add(1, std::memory_order_relaxed);
		try
		{
			m_Impl->Workers->Submit(&RunLoad, job.get(), 0, JobSystem::Priority::Background);
		}
		catch (...)
		{
			m_Impl->Tracker->Running.fetch_sub(1, std::memory_order_relaxed);
			m_Impl->Loads.pop_back();
			throw;
		}
		job.release();
		entry = asset;
		return AssetLease(std::move(asset));
	}

	void AssetStore::UseWorkers(JobSystem* workers) noexcept
	{
		m_Impl->Workers = workers;
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
				if (record.SourcePath != current->Record.SourcePath || record.Part != current->Record.Part ||
					color != current->ColorSpace)
					update.m_Impl->Changes.push_back({ current, m_Impl->Load(catalog, id) });
			}
		return update;
	}

	AssetReloadResult AssetStore::ApplyReloads()
	{
		Collect();
		AssetReloadResult result;
		// Finished background loads first; one whose asset was released is dropped.
		std::erase_if(m_Impl->Loads, [&](Impl::Pending& pending)
		{
			if (!pending.Load->Done.load(std::memory_order_acquire))
				return false;
			if (const auto asset = pending.Asset.lock())
				result.Loaded += m_Impl->Publish(*asset, *pending.Load) ? 1 : 0;
			return true;
		});
		for (const auto& [id, weak] : m_Impl->Entries)
		{
			auto current = weak.lock();
			if (!current || !current->Watch || current->State != AssetLoadState::Ready)
				continue;
			auto decoded = current->Watch->Take();
			if (!decoded)
				continue;
			try
			{
				const auto publishStart = std::chrono::steady_clock::now();
				auto candidate = m_Impl->Load(Impl::RecordOf(*current), id, std::move(decoded));
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
		result.Loading = m_Impl->Loads.size();
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

	AssetLoadState AssetLease::GetLoadState() const noexcept
	{
		return m_Asset ? m_Asset->State : AssetLoadState::Failed;
	}

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

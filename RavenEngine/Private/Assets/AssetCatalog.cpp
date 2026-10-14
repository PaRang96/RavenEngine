#include "Raven/Assets/AssetCatalog.hpp"

#include "Raven/Core/Paths.hpp"

#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Raven
{
	struct AssetCatalog::Impl
	{
		struct Entry
		{
			AssetRecord Record;
			std::optional<TextureAssetDesc> Texture;
		};

		void Register(const AssetRecord& record, std::optional<TextureColorSpace> colorSpace);
		const Entry& Get(const AssetId& id) const;

		std::map<AssetId, Entry> Entries;
	};

	void AssetCatalog::Impl::Register(const AssetRecord& record,
		std::optional<TextureColorSpace> colorSpace)
	{
		if (!record.Id.IsValid())
			throw std::invalid_argument("Cannot register a nil asset ID");
		const std::string identity = record.Id.ToString();
		switch (record.Kind)
		{
		case AssetKind::Texture:
		case AssetKind::Model:
		case AssetKind::Mesh:
		case AssetKind::Material:
		case AssetKind::Skeleton:
		case AssetKind::Animation:
		case AssetKind::Shader:
		case AssetKind::Scene:
			break;
		default:
			throw std::invalid_argument("Invalid asset kind: " + identity);
		}
		if (record.Kind == AssetKind::Texture && !colorSpace)
			throw std::invalid_argument("Texture registration requires TextureAssetDesc: " + identity);
		if (colorSpace && *colorSpace != TextureColorSpace::Linear && *colorSpace != TextureColorSpace::SRGB)
			throw std::invalid_argument("Invalid texture color space: " + identity);
		if (record.SourcePath.empty() || record.SourcePath.native().find(
			std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos)
			throw std::invalid_argument("Asset source path is empty or contains a null character: " + identity);
		// Use the same rooted-path/parent-escape policy as the public runtime utilities.
		(void)Paths::GetContentPath(record.SourcePath);
		auto sourcePath = record.SourcePath.lexically_normal();
		if (sourcePath == "." || !sourcePath.has_filename())
			throw std::invalid_argument("Asset source path must name a file: " + identity);

		Entry entry{ { record.Id, record.Kind, std::move(sourcePath) }, std::nullopt };
		if (colorSpace)
			entry.Texture.emplace(TextureAssetDesc{ record.Id, entry.Record.SourcePath, *colorSpace });
		if (!Entries.emplace(record.Id, std::move(entry)).second)
			throw std::invalid_argument("Duplicate asset ID: " + identity);
	}

	const AssetCatalog::Impl::Entry& AssetCatalog::Impl::Get(const AssetId& id) const
	{
		if (!id.IsValid())
			throw std::invalid_argument("Cannot look up a nil asset ID");
		const auto found = Entries.find(id);
		if (found == Entries.end())
			throw std::out_of_range("Asset ID is not registered: " + id.ToString());
		return found->second;
	}

	AssetCatalog::AssetCatalog()
		: m_Impl(std::make_unique<Impl>())
	{
	}

	AssetCatalog::~AssetCatalog() = default;

	void AssetCatalog::Register(const AssetRecord& record)
	{
		m_Impl->Register(record, std::nullopt);
	}

	void AssetCatalog::Register(const TextureAssetDesc& description)
	{
		m_Impl->Register({ description.Id, AssetKind::Texture, description.SourcePath }, description.ColorSpace);
	}

	const AssetRecord& AssetCatalog::Get(const AssetId& id) const
	{
		return m_Impl->Get(id).Record;
	}

	const TextureAssetDesc& AssetCatalog::GetTexture(AssetRef<TextureAsset> asset) const
	{
		const auto& entry = m_Impl->Get(asset.Id);
		if (!entry.Texture)
			throw std::invalid_argument("Asset ID does not refer to a texture: " + asset.Id.ToString());
		return *entry.Texture;
	}
}

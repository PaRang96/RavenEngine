#pragma once

#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Component.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Raven
{
	// Everything the engine knows per asset kind, in one row each (CQ-DATA-01). Adding a kind (B2
	// materials, B4 shaders, B7 model sub-assets, fonts) is one row here; the project reader and
	// writer, the catalog, the scene-dependency scan and the source checks all read this table.
	struct AssetKindInfo
	{
		AssetKind Kind = AssetKind::Unknown;
		// Name for messages, such as "PNG".
		std::string_view DisplayName;
		// The Project.ravenproject record keyword; empty while the kind has no project record.
		std::string_view Keyword;
		// Accepted source extensions, lowercase; none while the kind is not file-backed.
		std::array<std::string_view, 4> Extensions{};
		// The field-table tag of AssetRef<T> for this kind: HashText of its persisted type name (FD-08).
		std::uint64_t Tag = 0;
		// An AssetRef of this kind in a scene is a load dependency: leased with the scene and staged
		// with a player build.
		bool SceneDependency = false;
		// A Raven format that stores its own ID in its header; other file-backed kinds have a
		// .ravenasset sidecar (B0 contract 9).
		bool IdInHeader = false;

		bool IsFileBacked() const noexcept
		{
			return !Extensions[0].empty();
		}
	};

	namespace Detail
	{
		template<typename T>
		constexpr std::uint64_t AssetTag() noexcept
		{
			return ECS::Detail::HashText(AssetTypeTraits<T>::Name);
		}
	}

	inline constexpr std::array<AssetKindInfo, 11> AssetKinds{ {
		{ AssetKind::Texture, "PNG", "texture", { ".png" }, Detail::AssetTag<TextureAsset>(), true },
		{ AssetKind::Model, "model", {}, {}, Detail::AssetTag<ModelAsset>(), false },
		{ AssetKind::Mesh, "FBX", "mesh", { ".fbx" }, Detail::AssetTag<MeshAsset>(), true },
		{ AssetKind::Material, "material", {}, {}, Detail::AssetTag<MaterialAsset>(), false },
		{ AssetKind::Skeleton, "skeleton", {}, {}, Detail::AssetTag<SkeletonAsset>(), false },
		{ AssetKind::Animation, "animation", {}, {}, Detail::AssetTag<AnimationAsset>(), false },
		{ AssetKind::Shader, "shader", {}, {}, Detail::AssetTag<ShaderAsset>(), false },
		{ AssetKind::Scene, "scene", "scene", { ".ravenscene" }, Detail::AssetTag<SceneAsset>(), false, true },
		{ AssetKind::Audio, "audio", "audio", { ".wav", ".flac", ".mp3", ".ogg" }, Detail::AssetTag<AudioAsset>(), true },
		{ AssetKind::Sound, "sound event", "sound", { ".ravensound" }, Detail::AssetTag<SoundAsset>(), true, true },
		{ AssetKind::Font, "font", {}, {}, Detail::AssetTag<FontAsset>(), false } } };

	// The row of a kind; null for Unknown or a value outside the enum.
	inline const AssetKindInfo* FindAssetKind(AssetKind kind) noexcept
	{
		const auto found = std::find_if(AssetKinds.begin(), AssetKinds.end(),
			[kind](const AssetKindInfo& info) { return info.Kind == kind; });
		return found == AssetKinds.end() ? nullptr : &*found;
	}

	// The kind an AssetRef field's tag names; null for any other tag.
	inline const AssetKindInfo* FindAssetKindByTag(std::uint64_t tag) noexcept
	{
		const auto found = std::find_if(AssetKinds.begin(), AssetKinds.end(),
			[tag](const AssetKindInfo& info) { return info.Tag == tag; });
		return found == AssetKinds.end() ? nullptr : &*found;
	}

	// The kind of a project record keyword; null for a keyword no kind uses.
	inline const AssetKindInfo* FindAssetKindByKeyword(std::string_view keyword) noexcept
	{
		if (keyword.empty())
			return nullptr;
		const auto found = std::find_if(AssetKinds.begin(), AssetKinds.end(),
			[keyword](const AssetKindInfo& info) { return info.Keyword == keyword; });
		return found == AssetKinds.end() ? nullptr : &*found;
	}

	inline std::string_view AssetKindName(AssetKind kind) noexcept
	{
		const auto* info = FindAssetKind(kind);
		return info ? info->DisplayName : std::string_view("asset");
	}

	// Throws std::invalid_argument for a kind without a project record.
	inline std::string_view AssetKindKeyword(AssetKind kind)
	{
		const auto* info = FindAssetKind(kind);
		if (!info || info->Keyword.empty())
			throw std::invalid_argument("This asset kind has no project record: " +
				std::string(info ? info->DisplayName : std::string_view("unknown")));
		return info->Keyword;
	}

	inline std::string LowercaseExtension(const std::filesystem::path& path)
	{
		auto extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		return extension;
	}

	// Whether `path` is a source file of `kind`. Throws std::invalid_argument for a kind that is
	// not file-backed, so a new kind without extensions fails loudly instead of being misread.
	inline bool AcceptsSource(AssetKind kind, const std::filesystem::path& path)
	{
		const auto* info = FindAssetKind(kind);
		if (!info || !info->IsFileBacked())
			throw std::invalid_argument("This asset kind has no source files: " +
				std::string(info ? info->DisplayName : std::string_view("unknown")));
		const auto extension = LowercaseExtension(path);
		return std::find(info->Extensions.begin(), info->Extensions.end(), extension) != info->Extensions.end();
	}
}
